#include <compressor/archive/archive.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <filesystem>
#include <format>
#include <fstream>
#include <ios>
#include <istream>
#include <iterator>
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
    uint64_t offset = 0;
    uint64_t size = 0;
    uint32_t crc = 0;
    fs::path path; // where to write it with Sink::files; names it in errors
};

Target target(const Entry& file, fs::path path)
{
    return {.offset = file.offset, .size = file.size, .crc = file.crc, .path = std::move(path)};
}

enum class Sink : uint8_t
{
    files,   // each target into a new file at its path
    stream,  // the targets one after another into one stream
    discard, // only check the CRCs
};

// Splits the decoded data from `position` on into the targets, which must be in offset order,
// skipping the bytes between them and checking each one's CRC as it is completed.
class SplitOutputBuf : public std::streambuf
{
public:
    SplitOutputBuf(std::vector<Target> targets, uint64_t position, Sink sink,
                   std::streambuf* stream)
        : targets_(std::move(targets))
        , position_(position)
        , sink_(sink)
        , stream_(stream)
    {
    }

    const std::optional<Failure>& failure() const
    {
        return failure_;
    }

    // Checks that the data ended at `end` with every target complete.
    std::expected<void, Failure> finish(uint64_t end)
    {
        open_ready();
        if (!failure_ && (open_ || next_ != targets_.size() || position_ != end))
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
        while (done < count && open_ready())
        {
            const auto left = static_cast<uint64_t>(count - done);
            uint64_t step = left;
            if (open_)
            {
                step = std::min(targets_[next_].size - written_, left);
                if (!write(data + done, static_cast<std::streamsize>(step)))
                {
                    break;
                }
                written_ += step;
                if (written_ == targets_[next_].size)
                {
                    close_current();
                }
            }
            else if (next_ < targets_.size())
            {
                step = std::min(targets_[next_].offset - position_, left);
            }
            position_ += step;
            done += static_cast<std::streamsize>(step);
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
    // Opens the targets that start at position_, completing empty ones; false after a failure.
    bool open_ready()
    {
        while (!failure_ && !open_ && next_ < targets_.size())
        {
            const Target& next = targets_[next_];
            if (next.offset > position_)
            {
                break;
            }
            if (next.offset < position_)
            {
                failure_ = Failure{.error = Error::corrupt_index, .detail = {}};
                break;
            }
            if (sink_ == Sink::files && file_.open(next.path, std::ios::out | std::ios::binary |
                                                                  std::ios::noreplace) == nullptr)
            {
                failure_ = file_failure(next.path, {errno, std::generic_category()});
                break;
            }
            open_ = true;
            written_ = 0;
            crc_ = 0;
            if (next.size == 0)
            {
                close_current();
            }
        }
        return !failure_;
    }

    bool write(const char* data, std::streamsize count)
    {
        if (sink_ == Sink::files && file_.sputn(data, count) != count)
        {
            failure_ = file_failure(targets_[next_].path, {errno, std::generic_category()});
            return false;
        }
        if (sink_ == Sink::stream && stream_->sputn(data, count) != count)
        {
            failure_ = Failure{.error = Error::write_failed, .detail = {}};
            return false;
        }
        crc_ = crc32(as_bytes(data, count), crc_);
        return true;
    }

    void close_current()
    {
        const Target& target = targets_[next_];
        open_ = false;
        ++next_;
        if (sink_ == Sink::files && file_.close() == nullptr)
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
    uint64_t position_ = 0; // in the decoded data
    Sink sink_;
    std::streambuf* stream_;
    std::filebuf file_;
    bool open_ = false;
    uint64_t written_ = 0;
    uint32_t crc_ = 0;
    std::optional<Failure> failure_;
};

// Decodes only the blocks that hold the targets.
std::expected<void, Failure> decode_targets(IBlockDecoder& decoder, std::istream& in,
                                            const ArchiveInfo& info, std::vector<Target> targets,
                                            Sink sink, std::streambuf* stream = nullptr)
{
    const uint8_t log2 = info.block_size_log2;
    const std::vector<uint64_t>& table = info.index.block_offsets;
    uint64_t start = targets.empty() ? 0 : targets.front().offset;
    uint64_t end = start;
    std::vector<uint64_t> offsets;
    const uint64_t data_end = targets.empty() ? 0 : targets.back().offset + targets.back().size;
    if (start < data_end)
    {
        const uint64_t first = start >> log2;
        const uint64_t last = (data_end - 1) >> log2;
        offsets.assign(table.begin() + static_cast<std::ptrdiff_t>(first),
                       table.begin() + static_cast<std::ptrdiff_t>(last + 1));
        offsets.push_back(last + 1 < table.size() ? table[last + 1]
                                                  : info.index_offset - end_block_bytes);
        start = first << log2;
        end = std::min(info.total_size, (last + 1) << log2);
    }

    SplitOutputBuf split(std::move(targets), start, sink, stream);
    std::ostream out(&split);
    const auto decoded = decoder.decode_blocks(in, out, log2, offsets);
    if (split.failure())
    {
        return std::unexpected(*split.failure());
    }
    if (!decoded)
    {
        return std::unexpected(Failure{.error = decoded.error(), .detail = {}});
    }
    return split.finish(end);
}

std::string mode_string(const Entry& entry)
{
    std::string text = "-rwxrwxrwx";
    if (entry.type == EntryType::directory)
    {
        text[0] = 'd';
    }
    else if (entry.type == EntryType::symlink)
    {
        text[0] = 'l';
    }
    for (std::size_t bit = 0; bit < 9; ++bit)
    {
        if ((entry.mode & (1U << (8 - bit))) == 0)
        {
            text[1 + bit] = '-';
        }
    }
    // setuid, setgid and sticky show in the execute columns, upper case without execute.
    constexpr std::array<std::pair<unsigned, std::size_t>, 3> specials{
        {{04000U, 3}, {02000U, 6}, {01000U, 9}}};
    for (const auto& [bit, column] : specials)
    {
        if ((entry.mode & bit) != 0)
        {
            const bool execute = text[column] == 'x';
            const char letter = column == 9 ? 't' : 's';
            text[column] = execute ? letter : static_cast<char>(letter - 'a' + 'A');
        }
    }
    return text;
}

std::string utc_time(int64_t ns)
{
    using namespace std::chrono;
    const sys_time<nanoseconds> time{nanoseconds{ns}};
    const sys_days day = floor<days>(time);
    const year_month_day date{day};
    const hh_mm_ss clock{floor<seconds>(time - day)};
    return std::format("{:04}-{:02}-{:02} {:02}:{:02}:{:02}", static_cast<int>(date.year()),
                       static_cast<unsigned>(date.month()), static_cast<unsigned>(date.day()),
                       clock.hours().count(), clock.minutes().count(), clock.seconds().count());
}

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
    return extract_entries(decoder, in, *info, info->index.entries, directory);
}

std::string archive_path(std::string_view path)
{
    std::string normal = fs::path(path).lexically_normal().generic_string();
    while (normal.size() > 1 && normal.back() == '/')
    {
        normal.pop_back();
    }
    return normal;
}

std::optional<std::vector<Entry>> select_subtree(std::span<const Entry> entries,
                                                 std::string_view path)
{
    const auto root = std::ranges::find(entries, path, &Entry::path);
    if (path.empty() || root == entries.end())
    {
        return std::nullopt;
    }
    std::vector<Entry> selected{*root};
    selected.front().path.clear();
    if (root->type != EntryType::directory)
    {
        return selected;
    }
    for (const Entry& entry : entries)
    {
        if (entry.path.size() > path.size() && entry.path.starts_with(path) &&
            entry.path[path.size()] == '/')
        {
            selected.push_back(entry);
            selected.back().path.erase(0, path.size() + 1);
        }
    }
    return selected;
}

std::expected<void, Failure> extract_entries(IBlockDecoder& decoder, std::istream& in,
                                             const ArchiveInfo& info,
                                             std::span<const Entry> entries,
                                             const fs::path& directory)
{
    const auto path_of = [&directory](const Entry& entry) {
        return entry.path.empty() ? directory : directory / entry.path;
    };
    std::vector<Target> targets;
    for (const Entry& entry : entries)
    {
        if (entry.path.empty())
        {
            continue;
        }
        const fs::path path = path_of(entry);
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
            targets.push_back(target(entry, path));
        }
    }

    if (auto decoded = decode_targets(decoder, in, info, std::move(targets), Sink::files); !decoded)
    {
        return decoded;
    }

    for (const Entry& entry : entries)
    {
        if (entry.type == EntryType::symlink && !entry.path.empty())
        {
            const fs::path path = path_of(entry);
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
        const fs::path path = path_of(entry);
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

std::expected<void, Failure> extract_file(IBlockDecoder& decoder, std::istream& in,
                                          const ArchiveInfo& info, const Entry& file,
                                          std::ostream& out)
{
    std::vector<Target> targets{target(file, file.path)};
    return decode_targets(decoder, in, info, std::move(targets), Sink::stream, out.rdbuf());
}

std::expected<void, Failure> verify_archive(IBlockDecoder& decoder, std::istream& in,
                                            const ArchiveInfo& info)
{
    std::vector<Target> targets;
    for (const Entry& entry : info.index.entries)
    {
        if (entry.type == EntryType::file)
        {
            targets.push_back(target(entry, entry.path));
        }
    }
    return decode_targets(decoder, in, info, std::move(targets), Sink::discard);
}

void list_archive(const ArchiveInfo& info, bool details, std::ostream& out)
{
    for (const Entry& entry : info.index.entries)
    {
        if (details)
        {
            const uint64_t size =
                entry.type == EntryType::symlink ? entry.target.size() : entry.size;
            out << std::format("{} {:>12} {} ", mode_string(entry), size, utc_time(entry.mtime_ns));
        }
        out << entry.path;
        if (entry.type == EntryType::directory)
        {
            out << '/';
        }
        else if (details && entry.type == EntryType::symlink)
        {
            out << " -> " << entry.target;
        }
        out << '\n';
    }
}

} // namespace compressor
