#include <compressor/archive/archive.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <filesystem>
#include <fstream>
#include <ios>
#include <istream>
#include <optional>
#include <ostream>
#include <ranges>
#include <span>
#include <streambuf>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include <compressor/archive/index.h>
#include <compressor/archive/metadata.h>
#include <compressor/checksum/crc32.h>
#include <compressor/data/data.h>
#include <compressor/error.h>
#include <compressor/stream/decoder.h>
#include <compressor/stream/encoder.h>

namespace compressor
{

namespace
{

namespace fs = std::filesystem;

Failure file_failure(const fs::path& path, std::string_view problem)
{
    std::string detail = path.string();
    detail += ": ";
    detail += problem;
    return {.error = Error::file_failed, .detail = std::move(detail)};
}

Failure file_failure(const fs::path& path, std::error_code error)
{
    return file_failure(path, error.message());
}

std::span<const uint8_t> as_bytes(const char* data, std::streamsize size)
{
    return {reinterpret_cast<const uint8_t*>(data), static_cast<std::size_t>(size)};
}

// The files one after another as a single stream, measuring each as it is read.
class ConcatInputBuf : public std::streambuf
{
public:
    struct Read
    {
        uint64_t size = 0;
        uint32_t crc = 0;
    };

    explicit ConcatInputBuf(std::vector<fs::path> paths)
        : paths_(std::move(paths))
    {
    }

    // One per file read to its end.
    const std::vector<Read>& reads() const
    {
        return reads_;
    }

    const std::optional<Failure>& failure() const
    {
        return failure_;
    }

protected:
    int_type underflow() override
    {
        const std::streamsize got =
            read_some(buffer_.data(), static_cast<std::streamsize>(buffer_.size()));
        if (got == 0)
        {
            return traits_type::eof();
        }
        setg(buffer_.data(), buffer_.data(), buffer_.data() + got);
        return traits_type::to_int_type(buffer_[0]);
    }

    std::streamsize xsgetn(char* out, std::streamsize count) override
    {
        std::streamsize done = std::min<std::streamsize>(count, egptr() - gptr());
        if (done > 0)
        {
            std::memcpy(out, gptr(), static_cast<std::size_t>(done));
            gbump(static_cast<int>(done));
        }
        while (done < count)
        {
            const std::streamsize got = read_some(out + done, count - done);
            if (got == 0)
            {
                break;
            }
            done += got;
        }
        return done;
    }

private:
    // 0 only at the end of the last file or after a failure, which reads as the end of input.
    std::streamsize read_some(char* out, std::streamsize count)
    {
        while (!failure_)
        {
            if (!open_)
            {
                if (next_ == paths_.size())
                {
                    return 0;
                }
                if (file_.open(paths_[next_], std::ios::in | std::ios::binary) == nullptr)
                {
                    failure_ = file_failure(paths_[next_], {errno, std::generic_category()});
                    return 0;
                }
                open_ = true;
                current_ = {};
            }
            const std::streamsize got = file_.sgetn(out, count);
            if (got > 0)
            {
                current_.crc = crc32(as_bytes(out, got), current_.crc);
                current_.size += static_cast<uint64_t>(got);
                return got;
            }
            file_.close();
            open_ = false;
            reads_.push_back(current_);
            ++next_;
        }
        return 0;
    }

    std::vector<fs::path> paths_;
    std::size_t next_ = 0;
    std::filebuf file_;
    bool open_ = false;
    Read current_;
    std::vector<Read> reads_;
    std::optional<Failure> failure_;
    std::array<char, std::size_t{1} << 16U> buffer_{};
};

struct Target
{
    fs::path path;
    uint64_t size = 0;
    uint32_t crc = 0;
};

// Splits one stream into the target files, checking each one's CRC as it is closed.
class SplitOutputBuf : public std::streambuf
{
public:
    explicit SplitOutputBuf(std::vector<Target> targets)
        : targets_(std::move(targets))
    {
    }

    const std::optional<Failure>& failure() const
    {
        return failure_;
    }

    // Creates any empty files left and checks that every file got all of its bytes.
    std::expected<void, Failure> finish()
    {
        if (advance())
        {
            failure_ = Failure{.error = Error::corrupt_index, .detail = {}};
        }
        if (failure_)
        {
            return std::unexpected(*failure_);
        }
        return {};
    }

protected:
    std::streamsize xsputn(const char* data, std::streamsize count) override
    {
        std::streamsize done = 0;
        while (done < count && advance())
        {
            const auto room = static_cast<std::streamsize>(std::min<uint64_t>(
                targets_[next_].size - written_, static_cast<uint64_t>(count - done)));
            if (file_.sputn(data + done, room) != room)
            {
                failure_ = file_failure(targets_[next_].path, {errno, std::generic_category()});
                break;
            }
            crc_ = crc32(as_bytes(data + done, room), crc_);
            written_ += static_cast<uint64_t>(room);
            done += room;
        }
        return done;
    }

    int_type overflow(int_type c) override
    {
        if (traits_type::eq_int_type(c, traits_type::eof()))
        {
            return traits_type::not_eof(c);
        }
        const char byte = traits_type::to_char_type(c);
        return xsputn(&byte, 1) == 1 ? c : traits_type::eof();
    }

private:
    // True when targets_[next_] is open with room left; closes full files and opens the next.
    bool advance()
    {
        while (!failure_)
        {
            if (open_)
            {
                if (written_ < targets_[next_].size)
                {
                    return true;
                }
                close_current();
                continue;
            }
            if (next_ == targets_.size())
            {
                return false;
            }
            const fs::path& path = targets_[next_].path;
            if (file_.open(path, std::ios::out | std::ios::binary | std::ios::noreplace) == nullptr)
            {
                failure_ = file_failure(path, {errno, std::generic_category()});
                return false;
            }
            open_ = true;
            written_ = 0;
            crc_ = 0;
        }
        return false;
    }

    void close_current()
    {
        const Target& target = targets_[next_];
        open_ = false;
        ++next_;
        if (file_.close() == nullptr)
        {
            failure_ = file_failure(target.path, {errno, std::generic_category()});
        }
        else if (crc_ != target.crc)
        {
            failure_ = Failure{.error = Error::checksum_mismatch, .detail = target.path.string()};
        }
    }

    std::vector<Target> targets_;
    std::size_t next_ = 0;
    std::filebuf file_;
    bool open_ = false;
    uint64_t written_ = 0;
    uint32_t crc_ = 0;
    std::optional<Failure> failure_;
};

struct Pending
{
    fs::path path;
    std::string name; // relative to the root
};

// Pushes the children of `directory` so they pop in byte order of their names.
std::expected<void, Failure> push_children(std::vector<Pending>& stack, const fs::path& directory,
                                           const std::string& name)
{
    std::error_code error;
    std::vector<std::string> children;
    for (fs::directory_iterator it(directory, error), end; !error && it != end; it.increment(error))
    {
        children.push_back(it->path().filename().string());
    }
    if (error)
    {
        return std::unexpected(file_failure(directory, error));
    }
    std::ranges::sort(children, std::ranges::greater{});
    for (std::string& child : children)
    {
        fs::path path = directory / child;
        std::string relative = name;
        if (!relative.empty())
        {
            relative += '/';
        }
        relative += child;
        stack.push_back({.path = std::move(path), .name = std::move(relative)});
    }
    return {};
}

} // namespace

std::expected<std::vector<Entry>, Failure> scan_tree(const fs::path& root, const fs::path& exclude,
                                                     std::ostream& warnings)
{
    const auto excluded = read_metadata(exclude, true);
    std::vector<Entry> entries;
    std::vector<Pending> stack;
    if (auto pushed = push_children(stack, root, {}); !pushed)
    {
        return std::unexpected(pushed.error());
    }
    while (!stack.empty())
    {
        Pending next = std::move(stack.back());
        stack.pop_back();
        const auto metadata = read_metadata(next.path);
        if (!metadata)
        {
            return std::unexpected(file_failure(next.path, metadata.error()));
        }
        if (excluded && excluded->device == metadata->device && excluded->inode == metadata->inode)
        {
            continue;
        }
        if (!metadata->type)
        {
            warnings << "Skipping " << next.path.string()
                     << ": not a regular file, directory or symbolic link\n";
            continue;
        }
        if (next.name.size() > max_name_bytes)
        {
            return std::unexpected(file_failure(next.path, "path too long"));
        }
        Entry entry{.type = *metadata->type,
                    .path = next.name,
                    .mode = metadata->mode,
                    .mtime_ns = metadata->mtime_ns};
        if (entry.type == EntryType::file)
        {
            entry.size = metadata->size;
        }
        else if (entry.type == EntryType::symlink)
        {
            std::error_code error;
            entry.target = fs::read_symlink(next.path, error).string();
            if (error)
            {
                return std::unexpected(file_failure(next.path, error));
            }
            if (entry.target.size() > max_name_bytes)
            {
                return std::unexpected(file_failure(next.path, "symlink target too long"));
            }
        }
        entries.push_back(std::move(entry));
        if (entries.back().type == EntryType::directory)
        {
            if (auto pushed = push_children(stack, next.path, next.name); !pushed)
            {
                return std::unexpected(pushed.error());
            }
        }
    }
    return entries;
}

std::expected<void, Failure> write_archive(IBlockEncoder& encoder, const fs::path& root,
                                           std::vector<Entry> entries, std::ostream& out)
{
    auto files = entries | std::views::filter([](const Entry& entry) {
        return entry.type == EntryType::file;
    });
    std::vector<fs::path> paths;
    for (const Entry& entry : files)
    {
        paths.push_back(root / entry.path);
    }

    ConcatInputBuf concat(std::move(paths));
    std::istream in(&concat);
    auto layout = encoder.encode_frame(in, out, archive_flag);
    if (concat.failure())
    {
        return std::unexpected(*concat.failure());
    }
    if (!layout)
    {
        return std::unexpected(Failure{.error = layout.error(), .detail = {}});
    }
    if (concat.reads().size() != static_cast<std::size_t>(std::ranges::distance(files)))
    {
        return std::unexpected(Failure{.error = Error::read_failed, .detail = {}});
    }

    uint64_t offset = 0;
    auto read = concat.reads().begin();
    for (Entry& entry : files)
    {
        entry.offset = offset;
        entry.size = read->size;
        entry.crc = read->crc;
        offset += read->size;
        ++read;
    }
    write_index(out,
                {.block_offsets = std::move(layout->block_offsets), .entries = std::move(entries)},
                layout->size);
    out.flush();
    if (!out)
    {
        return std::unexpected(Failure{.error = Error::write_failed, .detail = {}});
    }
    return {};
}

bool is_archive(std::istream& in)
{
    FrameReader reader(in);
    const bool archive = reader.read_header() && (reader.flags() & archive_flag) != 0;
    in.clear();
    in.seekg(0);
    return archive;
}

std::expected<void, Failure> extract_archive(IBlockDecoder& decoder, std::istream& in,
                                             const fs::path& directory)
{
    auto info = read_archive_index(in);
    if (!info)
    {
        return std::unexpected(Failure{.error = info.error(), .detail = {}});
    }
    const std::vector<Entry>& entries = info->index.entries;

    std::vector<Target> targets;
    for (const Entry& entry : entries)
    {
        const fs::path path = directory / entry.path;
        if (entry.type == EntryType::directory)
        {
            std::error_code error;
            if (!fs::create_directory(path, error))
            {
                return std::unexpected(file_failure(
                    path, error ? error : std::make_error_code(std::errc::file_exists)));
            }
        }
        else if (entry.type == EntryType::file)
        {
            targets.push_back({.path = path, .size = entry.size, .crc = entry.crc});
        }
    }

    in.clear();
    in.seekg(0);
    SplitOutputBuf split(std::move(targets));
    std::ostream out(&split);
    const auto decoded = decoder.decode_frame(in, out, archive_flag);
    if (split.failure())
    {
        return std::unexpected(*split.failure());
    }
    if (!decoded)
    {
        return std::unexpected(Failure{.error = decoded.error(), .detail = {}});
    }
    if (auto finished = split.finish(); !finished)
    {
        return finished;
    }
    if (std::cmp_not_equal(static_cast<std::streamoff>(in.tellg()), info->index_offset))
    {
        return std::unexpected(Failure{.error = Error::corrupt_index, .detail = {}});
    }

    for (const Entry& entry : entries)
    {
        if (entry.type == EntryType::symlink)
        {
            const fs::path path = directory / entry.path;
            std::error_code error;
            fs::create_symlink(entry.target, path, error);
            if (error)
            {
                return std::unexpected(file_failure(path, error));
            }
        }
    }
    // Reversed, so a directory's time is set after its contents stop changing it.
    for (const Entry& entry : entries | std::views::reverse)
    {
        const fs::path path = directory / entry.path;
        std::error_code error;
        if (entry.type != EntryType::symlink)
        {
            error = set_mode(path, entry.mode);
        }
        if (!error)
        {
            error = set_mtime(path, entry.mtime_ns);
        }
        if (error)
        {
            return std::unexpected(file_failure(path, error));
        }
    }
    return {};
}

} // namespace compressor
