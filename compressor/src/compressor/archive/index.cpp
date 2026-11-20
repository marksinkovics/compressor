#include <compressor/archive/index.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <ios>
#include <istream>
#include <ostream>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <compressor/checksum/crc32.h>
#include <compressor/data/data.h>
#include <compressor/error.h>
#include <compressor/fmindex/fmindex.h>

namespace compressor
{

namespace
{

constexpr uint8_t end_marker = 0xFF;

template <std::size_t N> void put_le(std::string& out, uint64_t value)
{
    for (std::size_t i = 0; i < N; ++i)
    {
        out.push_back(static_cast<char>((value >> (8 * i)) & 0xFFU));
    }
}

void put_name(std::string& out, const std::string& name)
{
    put_le<2>(out, name.size());
    out += name;
}

// Reads little-endian fields from the index bytes; any read past the end makes ok() false.
class Cursor
{
public:
    explicit Cursor(std::span<const uint8_t> bytes)
        : bytes_(bytes)
    {
    }

    template <std::size_t N> uint64_t get()
    {
        if (bytes_.size() - position_ < N)
        {
            ok_ = false;
            position_ = bytes_.size();
            return 0;
        }
        uint64_t value = 0;
        for (std::size_t i = 0; i < N; ++i)
        {
            value |= static_cast<uint64_t>(bytes_[position_ + i]) << (8 * i);
        }
        position_ += N;
        return value;
    }

    std::string name()
    {
        const auto size = static_cast<std::size_t>(get<2>());
        if (bytes_.size() - position_ < size)
        {
            ok_ = false;
            position_ = bytes_.size();
            return {};
        }
        std::string text(reinterpret_cast<const char*>(bytes_.data() + position_), size);
        position_ += size;
        return text;
    }

    bool ok() const
    {
        return ok_;
    }

    bool at_end() const
    {
        return position_ == bytes_.size();
    }

    std::size_t remaining() const
    {
        return bytes_.size() - position_;
    }

private:
    std::span<const uint8_t> bytes_;
    std::size_t position_ = 0;
    bool ok_ = true;
};

// Smallest encoded entry: type, mode, mtime, an empty path length.
constexpr std::size_t min_entry_bytes = 1 + 2 + 8 + 2;

bool valid_path(std::string_view path)
{
    if (path.empty() || path.size() > max_name_bytes || path.contains('\0'))
    {
        return false;
    }
    std::size_t start = 0;
    while (true)
    {
        const std::size_t end = std::min(path.find('/', start), path.size());
        const std::string_view part = path.substr(start, end - start);
        if (part.empty() || part == "." || part == "..")
        {
            return false;
        }
        if (end == path.size())
        {
            return true;
        }
        start = end + 1;
    }
}

std::string_view parent_of(std::string_view path)
{
    const std::size_t slash = path.rfind('/');
    return slash == std::string_view::npos ? std::string_view{} : path.substr(0, slash);
}

std::expected<void, Error> read_at(std::istream& istream, uint64_t offset, std::span<uint8_t> bytes)
{
    istream.clear();
    if (!istream.seekg(static_cast<std::streamoff>(offset)))
    {
        return std::unexpected(Error::read_failed);
    }
    istream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (istream.gcount() != static_cast<std::streamsize>(bytes.size()))
    {
        return std::unexpected(istream.bad() ? Error::read_failed : Error::truncated);
    }
    return {};
}

uint64_t block_count(uint64_t total_size, uint8_t block_size_log2)
{
    const uint64_t block_size = uint64_t{1} << block_size_log2;
    return (total_size + block_size - 1) >> block_size_log2;
}

} // namespace

void write_index(std::ostream& ostream, const ArchiveIndex& index, uint64_t index_offset)
{
    std::string bytes;
    for (const uint64_t offset : index.block_offsets)
    {
        put_le<8>(bytes, offset);
    }
    put_le<4>(bytes, index.entries.size());
    for (const Entry& entry : index.entries)
    {
        bytes.push_back(static_cast<char>(entry.type));
        put_le<2>(bytes, entry.mode);
        put_le<8>(bytes, static_cast<uint64_t>(entry.mtime_ns));
        put_name(bytes, entry.path);
        if (entry.type == EntryType::file)
        {
            put_le<8>(bytes, entry.offset);
            put_le<8>(bytes, entry.size);
            put_le<4>(bytes, entry.crc);
        }
        else if (entry.type == EntryType::symlink)
        {
            put_name(bytes, entry.target);
        }
    }
    const uint32_t crc =
        crc32(std::span(reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size()));
    put_le<8>(bytes, index_offset);
    put_le<4>(bytes, crc);
    bytes.append(index_magic.begin(), index_magic.end());
    ostream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

std::expected<void, Error> validate_index(const ArchiveIndex& index, uint8_t block_size_log2,
                                          uint64_t total_size, uint64_t end_offset)
{
    const std::vector<uint64_t>& blocks = index.block_offsets;
    if (blocks.size() != block_count(total_size, block_size_log2) ||
        (!blocks.empty() && blocks.front() != frame_header_bytes))
    {
        return std::unexpected(Error::corrupt_index);
    }
    for (std::size_t i = 0; i < blocks.size(); ++i)
    {
        const uint64_t next = i + 1 < blocks.size() ? blocks[i + 1] : end_offset;
        if (next < blocks[i] || next - blocks[i] < block_header_bytes + 1)
        {
            return std::unexpected(Error::corrupt_index);
        }
    }

    std::unordered_set<std::string_view> paths;
    std::unordered_set<std::string_view> directories;
    uint64_t offset = 0;
    std::unordered_map<uint64_t, const Entry*> stored; // files with data, by offset
    for (const Entry& entry : index.entries)
    {
        if (entry.type > EntryType::symlink || entry.mode > max_mode || !valid_path(entry.path) ||
            !paths.insert(entry.path).second)
        {
            return std::unexpected(Error::corrupt_index);
        }
        const std::string_view parent = parent_of(entry.path);
        if (!parent.empty() && !directories.contains(parent))
        {
            return std::unexpected(Error::corrupt_index);
        }
        switch (entry.type)
        {
        case EntryType::directory:
            directories.insert(entry.path);
            break;
        case EntryType::symlink:
            if (entry.target.empty() || entry.target.contains('\0'))
            {
                return std::unexpected(Error::corrupt_index);
            }
            break;
        case EntryType::file:
            if (entry.offset == offset && entry.size <= total_size - offset)
            {
                if (entry.size > 0)
                {
                    stored.emplace(offset, &entry);
                }
                offset += entry.size;
                break;
            }
            // Otherwise a duplicate, sharing all of an earlier file's data.
            if (const auto original = stored.find(entry.offset);
                entry.size == 0 || original == stored.end() ||
                original->second->size != entry.size || original->second->crc != entry.crc)
            {
                return std::unexpected(Error::corrupt_index);
            }
            break;
        }
    }
    if (offset != total_size)
    {
        return std::unexpected(Error::corrupt_index);
    }
    return {};
}

std::expected<ArchiveInfo, Error> read_archive_index(std::istream& istream)
{
    istream.clear();
    if (!istream.seekg(0, std::ios::end))
    {
        return std::unexpected(Error::read_failed);
    }
    const auto file_size = static_cast<uint64_t>(istream.tellg());
    istream.seekg(0);

    FrameReader reader(istream);
    auto block_size_log2 = reader.read_header();
    if (!block_size_log2)
    {
        return std::unexpected(block_size_log2.error());
    }
    if ((reader.flags() & archive_flag) == 0)
    {
        return std::unexpected(Error::corrupt_index);
    }
    if (file_size < frame_header_bytes + end_block_bytes + trailer_bytes)
    {
        return std::unexpected(Error::truncated);
    }

    // With a search index, the archive ends where that section starts.
    uint64_t archive_end = file_size;
    std::optional<std::pair<uint64_t, uint64_t>> search_index;
    if ((reader.flags() & search_index_flag) != 0)
    {
        std::array<uint8_t, search_trailer_bytes> search_trailer{};
        if (file_size < search_trailer_bytes + trailer_bytes)
        {
            return std::unexpected(Error::truncated);
        }
        if (auto read = read_at(istream, file_size - search_trailer_bytes, search_trailer); !read)
        {
            return std::unexpected(read.error());
        }
        const uint64_t section = Cursor(search_trailer).get<8>();
        if (!std::equal(search_index_magic.begin(), search_index_magic.end(),
                        search_trailer.begin() + 12) ||
            section > file_size - search_trailer_bytes || section < trailer_bytes)
        {
            return std::unexpected(Error::corrupt_index);
        }
        archive_end = section;
        search_index = std::pair{section, file_size - search_trailer_bytes};
    }
    if (archive_end < frame_header_bytes + end_block_bytes + trailer_bytes)
    {
        return std::unexpected(Error::corrupt_index);
    }

    std::array<uint8_t, trailer_bytes> trailer{};
    if (auto read = read_at(istream, archive_end - trailer_bytes, trailer); !read)
    {
        return std::unexpected(read.error());
    }
    Cursor trailer_fields(trailer);
    const uint64_t index_offset = trailer_fields.get<8>();
    const auto index_crc = static_cast<uint32_t>(trailer_fields.get<4>());
    if (!std::equal(index_magic.begin(), index_magic.end(), trailer.begin() + 12) ||
        index_offset < frame_header_bytes + end_block_bytes ||
        index_offset > archive_end - trailer_bytes)
    {
        return std::unexpected(Error::corrupt_index);
    }

    std::array<uint8_t, end_block_bytes> end_block{};
    if (auto read = read_at(istream, index_offset - end_block_bytes, end_block); !read)
    {
        return std::unexpected(read.error());
    }
    if (end_block[0] != end_marker)
    {
        return std::unexpected(Error::corrupt_index);
    }
    ArchiveInfo info{.block_size_log2 = *block_size_log2,
                     .total_size = Cursor(std::span(end_block).subspan(1)).get<8>(),
                     .index_offset = index_offset,
                     .index = {},
                     .search_index = search_index};

    std::vector<uint8_t> bytes(archive_end - trailer_bytes - index_offset);
    if (auto read = read_at(istream, index_offset, bytes); !read)
    {
        return std::unexpected(read.error());
    }
    if (crc32(bytes) != index_crc)
    {
        return std::unexpected(Error::corrupt_index);
    }

    Cursor cursor(bytes);
    const uint64_t blocks = block_count(info.total_size, info.block_size_log2);
    if (blocks > cursor.remaining() / 8)
    {
        return std::unexpected(Error::corrupt_index);
    }
    info.index.block_offsets.resize(blocks);
    for (uint64_t& offset : info.index.block_offsets)
    {
        offset = cursor.get<8>();
    }
    const uint64_t entries = cursor.get<4>();
    if (entries > cursor.remaining() / min_entry_bytes)
    {
        return std::unexpected(Error::corrupt_index);
    }
    info.index.entries.resize(entries);
    for (Entry& entry : info.index.entries)
    {
        const uint64_t type = cursor.get<1>();
        if (type > static_cast<uint64_t>(EntryType::symlink))
        {
            return std::unexpected(Error::corrupt_index);
        }
        entry.type = static_cast<EntryType>(type);
        entry.mode = static_cast<uint16_t>(cursor.get<2>());
        entry.mtime_ns = static_cast<int64_t>(cursor.get<8>());
        entry.path = cursor.name();
        if (entry.type == EntryType::file)
        {
            entry.offset = cursor.get<8>();
            entry.size = cursor.get<8>();
            entry.crc = static_cast<uint32_t>(cursor.get<4>());
        }
        else if (entry.type == EntryType::symlink)
        {
            entry.target = cursor.name();
        }
        if (!cursor.ok())
        {
            return std::unexpected(Error::corrupt_index);
        }
    }
    if (!cursor.at_end())
    {
        return std::unexpected(Error::corrupt_index);
    }
    if (auto valid = validate_index(info.index, info.block_size_log2, info.total_size,
                                    index_offset - end_block_bytes);
        !valid)
    {
        return std::unexpected(valid.error());
    }
    return info;
}

} // namespace compressor
