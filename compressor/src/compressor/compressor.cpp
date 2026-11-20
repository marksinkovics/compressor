#include <compressor/compressor.h>

#include <algorithm>
#include <cstdio>
#include <expected>
#include <filesystem>
#include <format>
#include <fstream>
#include <ios>
#include <iostream>
#include <istream>
#include <memory>
#include <optional>
#include <ostream>
#include <random>
#include <span>
#include <sstream>
#include <streambuf>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#include <compressor/archive/archive.h>
#include <compressor/archive/index.h>
#include <compressor/archive/metadata.h>
#include <compressor/data/data.h>
#include <compressor/deflate/deflate.h>
#include <compressor/deflate/zip.h>
#include <compressor/engine/engine.h>
#include <compressor/error.h>
#include <compressor/fmindex/fmindex.h>
#include <compressor/search/search.h>
#include <compressor/stats/stats.h>
#include <compressor/stream/decoder.h>
#include <compressor/stream/encoder.h>
#include <compressor/stream/stdio.h>
#include <compressor/thread/pool.h>

#include <instrumentor/instrumentor.h>

namespace compressor
{

namespace
{

namespace fs = std::filesystem;

unsigned thread_count(unsigned requested)
{
    return requested == 0 ? default_thread_count() : requested;
}

struct TempFile
{
    fs::path path;
    std::unique_ptr<OutputFile> file;
};

// Retries only when the random name is taken, not when the directory can't be written.
// Retries only when the random name is taken, not when the directory can't be written.
std::optional<TempFile> create_temp_next_to(const fs::path& output)
{
    std::random_device random;
    for (int attempt = 0; attempt < 8; ++attempt)
    {
        std::ostringstream name;
        name << '.' << output.filename().string() << '.' << std::hex << random() << ".tmp";
        TempFile temp{.path = output.parent_path() / name.str(), .file = nullptr};
        temp.file = OutputFile::create(temp.path);
        if (temp.file)
        {
            return temp;
        }
        std::error_code error;
        if (!fs::exists(temp.path, error))
        {
            break;
        }
    }
    return std::nullopt;
}

// Regular files and missing outputs are replaced atomically; anything else is a device or pipe.
bool replaces_output(const fs::path& output)
{
    std::error_code error;
    const fs::file_status status = fs::status(output, error);
    return !fs::exists(status) || fs::is_regular_file(status);
}

bool is_standard(const fs::path& path)
{
    return path.native() == fs::path(standard_stream).native();
}

std::string display_name(const fs::path& path, std::string_view standard_name)
{
    return is_standard(path) ? std::string(standard_name) : path.string();
}

using Result = std::expected<void, Failure>;

void report(std::ostream& err, std::string_view action, const fs::path& input,
            const Failure& failure)
{
    err << "Cannot " << action << ' ' << display_name(input, "standard input") << ": "
        << describe(failure.error);
    if (!failure.detail.empty())
    {
        err << " (" << failure.detail << ')';
    }
    err << '\n';
}

template <class Write> Result write_in_place(Write& write, std::ostream& out)
{
    Result result = write(out);
    out.flush();
    if (result && out.fail())
    {
        result = std::unexpected(Failure{.error = Error::write_failed, .detail = {}});
    }
    return result;
}

// `write` fills the output stream; `report_failure` gets its failure.
template <class Write, class Report>
bool write_output(const StandardStreams& streams, const fs::path& output, Write write,
                  Report report_failure)
{
    std::ostream& err = streams.err;
    if (is_standard(output))
    {
        if (auto result = write_in_place(write, streams.out); !result)
        {
            report_failure(result.error());
            return false;
        }
        return true;
    }

    if (!replaces_output(output))
    {
        std::ofstream out(output, std::ios::out | std::ios::binary);
        if (!out)
        {
            err << "Cannot open output file: " << output.string() << '\n';
            return false;
        }
        if (auto result = write_in_place(write, out); !result)
        {
            report_failure(result.error());
            return false;
        }
        return true;
    }

    std::optional<TempFile> temp = create_temp_next_to(output);
    if (!temp)
    {
        err << "Cannot open output file: " << output.string() << '\n';
        return false;
    }
    Result result = write(temp->file->stream());
    const bool closed = temp->file->close() && !temp->file->stream().fail();
    if (result && !closed)
    {
        result = std::unexpected(Failure{.error = Error::write_failed, .detail = {}});
    }
    std::error_code error;
    if (!result)
    {
        fs::remove(temp->path, error);
        report_failure(result.error());
        return false;
    }
    fs::rename(temp->path, output, error);
    if (error)
    {
        err << "Cannot replace output file " << output.string() << ": " << error.message() << '\n';
        fs::remove(temp->path, error);
        return false;
    }
    return true;
}

// The standard input, or `input` opened into `file`; null when it can't be opened.
std::istream* open_input(const StandardStreams& streams, const fs::path& input, std::ifstream& file)
{
    if (is_standard(input))
    {
        return &streams.in;
    }
    file.open(input, std::ios::in | std::ios::binary);
    if (!file)
    {
        streams.err << "Cannot open input file: " << input.string() << '\n';
        return nullptr;
    }
    return &file;
}

template <class Coder>
bool transcode(const StandardStreams& streams, std::string_view action, const fs::path& input,
               const fs::path& output, Coder code)
{
    std::ifstream file;
    std::istream* in = open_input(streams, input, file);
    if (in == nullptr)
    {
        return false;
    }
    return write_output(streams, output, [&](std::ostream& out) -> Result {
        std::expected<void, Error> result = code(*in, out);
        // Closed before the output is renamed, which may replace the input.
        file.close();
        if (!result)
        {
            return std::unexpected(Failure{.error = result.error(), .detail = {}});
        }
        return {};
    }, [&](const Failure& failure) {
        report(streams.err, action, input, failure);
    });
}

std::optional<fs::path> create_temp_directory_next_to(const fs::path& output)
{
    std::random_device random;
    for (int attempt = 0; attempt < 8; ++attempt)
    {
        std::ostringstream name;
        name << '.' << output.filename().string() << '.' << std::hex << random() << ".tmp";
        fs::path path = output.parent_path() / name.str();
        std::error_code error;
        if (fs::create_directory(path, error))
        {
            return path;
        }
        if (error || !fs::exists(path, error))
        {
            break;
        }
    }
    return std::nullopt;
}

bool is_archive_file(const fs::path& input)
{
    if (is_standard(input))
    {
        return false;
    }
    std::ifstream file(input, std::ios::in | std::ios::binary);
    return file && is_archive(file);
}

struct OpenArchive
{
    std::ifstream file;
    ArchiveInfo info;
};

std::optional<OpenArchive> open_archive(std::ostream& err, std::string_view action,
                                        const fs::path& input)
{
    if (is_standard(input))
    {
        err << "Cannot " << action
            << " standard input: a folder archive must be read from a file\n";
        return std::nullopt;
    }
    OpenArchive archive;
    archive.file.open(input, std::ios::in | std::ios::binary);
    if (!archive.file)
    {
        err << "Cannot open input file: " << input.string() << '\n';
        return std::nullopt;
    }
    if (!is_archive(archive.file))
    {
        err << "Cannot " << action << ' ' << input.string() << ": it is not a folder archive\n";
        return std::nullopt;
    }
    auto info = read_archive_index(archive.file);
    if (!info)
    {
        report(err, action, input, {.error = info.error(), .detail = {}});
        return std::nullopt;
    }
    archive.info = std::move(*info);
    return archive;
}

// A new directory or symlink can't go to the standard output or replace anything.
bool is_new_path(std::ostream& err, std::string_view what, const fs::path& output)
{
    if (is_standard(output))
    {
        err << "Cannot write " << what << " to standard output; name the new path with -o\n";
        return false;
    }
    std::error_code error;
    if (fs::exists(fs::symlink_status(output, error)))
    {
        err << "Cannot write " << what << ": the output already exists: " << output.string()
            << '\n';
        return false;
    }
    return true;
}

// Builds the directory next to `output` and renames it into place.
bool extract_to_new_directory(IBlockDecoder& decoder, std::ostream& err, std::string_view action,
                              const fs::path& input, OpenArchive& archive,
                              std::span<const Entry> entries, const fs::path& output)
{
    const std::optional<fs::path> temp = create_temp_directory_next_to(output);
    if (!temp)
    {
        err << "Cannot create output directory: " << output.string() << '\n';
        return false;
    }
    std::error_code error;
    if (auto result = extract_entries(decoder, archive.file, archive.info, entries, *temp); !result)
    {
        fs::remove_all(*temp, error);
        report(err, action, input, result.error());
        return false;
    }
    fs::rename(*temp, output, error);
    if (error)
    {
        err << "Cannot create output directory " << output.string() << ": " << error.message()
            << '\n';
        fs::remove_all(*temp, error);
        return false;
    }
    return true;
}

class SearchVisitor : public FileVisitor
{
public:
    explicit SearchVisitor(LineSearcher& searcher)
        : searcher_(searcher)
    {
    }

    void begin(std::span<const fs::path> paths) override
    {
        std::vector<std::string> names;
        names.reserve(paths.size());
        for (const fs::path& path : paths)
        {
            names.push_back(path.generic_string());
        }
        searcher_.begin(std::move(names), true);
    }

    void data(std::string_view bytes) override
    {
        searcher_.feed(bytes);
    }

    void end() override
    {
        searcher_.end();
    }

private:
    LineSearcher& searcher_;
};

class SearchBuf : public std::streambuf
{
public:
    explicit SearchBuf(LineSearcher& searcher)
        : searcher_(searcher)
    {
    }

protected:
    std::streamsize xsputn(const char* data, std::streamsize count) override
    {
        searcher_.feed({data, static_cast<std::size_t>(count)});
        return count;
    }

    int_type overflow(int_type c) override
    {
        if (!traits_type::eq_int_type(c, traits_type::eof()))
        {
            const char byte = traits_type::to_char_type(c);
            searcher_.feed({&byte, 1});
        }
        return traits_type::not_eof(c);
    }

private:
    LineSearcher& searcher_;
};

class DiscardBuf : public std::streambuf
{
protected:
    std::streamsize xsputn(const char* /*data*/, std::streamsize count) override
    {
        return count;
    }

    int_type overflow(int_type c) override
    {
        return traits_type::not_eof(c);
    }
};

} // namespace

StandardStreams process_streams()
{
    static StdioInputBuf in_buf(stdin);
    static StdioOutputBuf out_buf(stdout);
    static std::istream in(&in_buf);
    static std::ostream out(&out_buf);
    return {.in = in, .out = out, .err = std::cerr};
}

Compressor::Compressor()
    : Compressor(CompressorSettings{})
{
}

Compressor::Compressor(CompressorSettings settings)
    : Compressor(std::make_unique<BlockEncoder>(
                     [level = settings.level] {
                         return std::make_unique<Engine>(default_block_size_log2, level);
                     }, default_block_size_log2, thread_count(settings.threads)),
                 std::make_unique<BlockDecoder>(
                     [] {
                         return std::make_unique<Engine>();
                     }, thread_count(settings.threads)),
                 process_streams(), settings.search_index)
{
}

Compressor::Compressor(std::unique_ptr<IBlockEncoder> encoder,
                       std::unique_ptr<IBlockDecoder> decoder, StandardStreams streams,
                       bool search_index)
    : encoder_(std::move(encoder))
    , decoder_(std::move(decoder))
    , streams_(streams)
    , search_index_(search_index)
{
}

bool Compressor::encode(const fs::path& input, const fs::path& output)
{
    PROFILE_SESSION("Compressor::Encode")
    std::error_code error;
    if (!is_standard(input) && fs::is_directory(input, error))
    {
        return encode_folder(input, output);
    }
    return transcode(streams_, "encode", input, output,
                     [this](std::istream& in, std::ostream& out) {
        return encoder_->encode(in, out);
    });
}

bool Compressor::decode(const fs::path& input, const fs::path& output)
{
    PROFILE_SESSION("Compressor::Decode")
    if (is_archive_file(input))
    {
        return decode_folder(input, output);
    }
    return transcode(streams_, "decode", input, output,
                     [this](std::istream& in, std::ostream& out) {
        return decoder_->decode(in, out);
    });
}

bool Compressor::encode_as(const fs::path& input, const fs::path& output, OutputFormat format)
{
    PROFILE_SESSION("Compressor::EncodeAs")
    std::ostream& err = streams_.err;
    std::error_code error;
    const bool folder = !is_standard(input) && fs::is_directory(input, error);
    switch (format)
    {
    case OutputFormat::ingot:
        return encode(input, output);
    case OutputFormat::gzip:
    {
        if (folder)
        {
            err << "Cannot encode " << input.string()
                << " as gzip: it is a folder; use the zip format\n";
            return false;
        }
        GzipHeader header;
        if (!is_standard(input))
        {
            header.name = input.filename().string();
            const auto metadata = read_metadata(input, true);
            if (metadata && metadata->mtime_ns >= 0)
            {
                header.mtime = static_cast<uint32_t>(
                    std::min<int64_t>(metadata->mtime_ns / 1'000'000'000, 0xFFFFFFFF));
            }
        }
        return transcode(
            streams_, "encode", input, output,
            [&header](std::istream& in, std::ostream& out) -> std::expected<void, Error> {
            if (auto written = write_gzip(in, out, header); !written)
            {
                return std::unexpected(written.error());
            }
            return {};
        });
    }
    case OutputFormat::zip:
    {
        if (!folder)
        {
            err << "Cannot encode " << display_name(input, "standard input")
                << " as zip: it is not a folder; use the gzip format for a file\n";
            return false;
        }
        const auto report_failure = [&](const Failure& failure) {
            report(err, "encode", input, failure);
        };
        auto entries = scan_tree(input, is_standard(output) ? fs::path{} : output, err);
        if (!entries)
        {
            report_failure(entries.error());
            return false;
        }
        return write_output(streams_, output, [&](std::ostream& out) {
            return write_zip(input, *entries, out);
        }, report_failure);
    }
    }
    return false;
}

bool Compressor::encode_folder(const fs::path& input, const fs::path& output)
{
    const auto report_failure = [&](const Failure& failure) {
        report(streams_.err, "encode", input, failure);
    };
    auto entries = scan_tree(input, is_standard(output) ? fs::path{} : output, streams_.err);
    if (!entries)
    {
        report_failure(entries.error());
        return false;
    }
    return write_output(streams_, output, [&](std::ostream& out) {
        return write_archive(*encoder_, input, std::move(*entries), out, search_index_);
    }, report_failure);
}

bool Compressor::decode_folder(const fs::path& input, const fs::path& output)
{
    std::ostream& err = streams_.err;
    if (!is_new_path(err, "a folder archive", output))
    {
        return false;
    }
    auto archive = open_archive(err, "decode", input);
    return archive && extract_to_new_directory(*decoder_, err, "decode", input, *archive,
                                               archive->info.index.entries, output);
}

bool Compressor::list(const fs::path& archive, bool details)
{
    auto opened = open_archive(streams_.err, "list", archive);
    if (!opened)
    {
        return false;
    }
    list_archive(opened->info, details, streams_.out);
    streams_.out.flush();
    if (!streams_.out)
    {
        streams_.err << "Cannot write the listing\n";
        return false;
    }
    return true;
}

bool Compressor::extract(const fs::path& archive, const std::string& path, const fs::path& output)
{
    PROFILE_SESSION("Compressor::Extract")
    std::ostream& err = streams_.err;
    constexpr std::string_view action = "extract from";
    auto opened = open_archive(err, action, archive);
    if (!opened)
    {
        return false;
    }
    const std::string name = archive_path(path);
    auto selected = select_subtree(opened->info.index.entries, name);
    if (!selected)
    {
        err << "Cannot extract " << path << ": it is not in " << archive.string() << '\n';
        return false;
    }
    Entry& root = selected->front();
    const std::string what =
        std::format("{} {}", root.type == EntryType::directory ? "directory" : "symlink", name);
    std::error_code error;
    switch (root.type)
    {
    case EntryType::file:
    {
        root.path = name;
        const bool written = write_output(streams_, output, [&](std::ostream& out) {
            return extract_file(*decoder_, opened->file, opened->info, root, out);
        }, [&](const Failure& failure) {
            report(err, action, archive, failure);
        });
        if (!written || is_standard(output) ||
            !fs::is_regular_file(fs::symlink_status(output, error)))
        {
            return written;
        }
        error = set_mode(output, root.mode);
        break;
    }
    case EntryType::directory:
        return is_new_path(err, what, output) &&
               extract_to_new_directory(*decoder_, err, action, archive, *opened, *selected,
                                        output);
    case EntryType::symlink:
        if (!is_new_path(err, what, output))
        {
            return false;
        }
        fs::create_symlink(root.target, output, error);
        break;
    }
    if (!error)
    {
        error = set_mtime(output, root.mtime_ns);
    }
    if (error)
    {
        err << "Cannot write " << output.string() << ": " << error.message() << '\n';
        return false;
    }
    return true;
}

bool Compressor::verify(const fs::path& input)
{
    PROFILE_SESSION("Compressor::Verify")
    std::ostream& err = streams_.err;
    std::expected<void, Failure> result;
    if (is_archive_file(input))
    {
        auto archive = open_archive(err, "verify", input);
        if (!archive)
        {
            return false;
        }
        result = verify_archive(*decoder_, archive->file, archive->info);
        if (result && archive->info.search_index)
        {
            const auto [section, end] = *archive->info.search_index;
            if (auto read =
                    read_search_index(archive->file, section, end, archive->info.total_size);
                !read)
            {
                result = std::unexpected(Failure{.error = read.error(), .detail = "search index"});
            }
        }
    }
    else
    {
        std::ifstream file;
        std::istream* in = open_input(streams_, input, file);
        if (in == nullptr)
        {
            return false;
        }
        DiscardBuf discard;
        std::ostream out(&discard);
        if (auto decoded = decoder_->decode(*in, out); !decoded)
        {
            result = std::unexpected(Failure{.error = decoded.error(), .detail = {}});
        }
    }
    if (!result)
    {
        report(err, "verify", input, result.error());
        return false;
    }
    streams_.out << display_name(input, "standard input") << ": OK\n";
    streams_.out.flush();
    return true;
}

bool Compressor::stats(const fs::path& input, const StatsOptions& options)
{
    PROFILE_SESSION("Compressor::Stats")
    std::ostream& err = streams_.err;
    if (is_standard(input))
    {
        err << "Cannot analyse standard input: stats needs a file\n";
        return false;
    }
    std::ifstream file(input, std::ios::in | std::ios::binary);
    if (!file)
    {
        err << "Cannot open input file: " << input.string() << '\n';
        return false;
    }
    const auto collected = collect_stats(file);
    if (!collected)
    {
        report(err, "analyse", input, {.error = collected.error(), .detail = {}});
        return false;
    }
    if (options.graph.empty() || !is_standard(options.graph))
    {
        print_stats(*collected, options.report, streams_.out);
        streams_.out.flush();
    }
    if (!options.graph.empty())
    {
        const bool written =
            write_output(streams_, options.graph, [&](std::ostream& out) -> Result {
            write_content_graph(*collected, out);
            return {};
        }, [&](const Failure& failure) {
            report(err, "write the graph of", input, failure);
        });
        if (!written)
        {
            return false;
        }
    }
    if (!streams_.out)
    {
        err << "Cannot write the statistics\n";
        return false;
    }
    return true;
}

SearchResult Compressor::grep(const fs::path& input, const SearchOptions& options,
                              const std::vector<std::string>& paths)
{
    PROFILE_SESSION("Compressor::Grep")
    std::ostream& err = streams_.err;
    constexpr std::string_view action = "search";
    if (options.pattern.contains('\n'))
    {
        err << "Cannot search for a pattern with a line break\n";
        return SearchResult::failed;
    }
    LineSearcher searcher(options, streams_.out);
    std::expected<void, Failure> result;
    if (is_archive_file(input))
    {
        auto archive = open_archive(err, action, input);
        if (!archive)
        {
            return SearchResult::failed;
        }
        const auto selected = select_paths(archive->info.index.entries, paths);
        if (!selected)
        {
            err << "Cannot search " << selected.error() << ": it is not in " << input.string()
                << '\n';
            return SearchResult::failed;
        }
        SearchVisitor visitor(searcher);
        result = visit_files(*decoder_, archive->file, archive->info, *selected, visitor);
    }
    else
    {
        if (!paths.empty())
        {
            err << "Cannot search paths in " << display_name(input, "standard input")
                << ": it is not a folder archive\n";
            return SearchResult::failed;
        }
        std::ifstream file;
        std::istream* in = open_input(streams_, input, file);
        if (in == nullptr)
        {
            return SearchResult::failed;
        }
        searcher.begin({display_name(input, "(standard input)")}, false);
        SearchBuf buffer(searcher);
        std::ostream out(&buffer);
        if (auto decoded = decoder_->decode(*in, out); !decoded)
        {
            result = std::unexpected(Failure{.error = decoded.error(), .detail = {}});
        }
        else
        {
            searcher.end();
        }
    }
    streams_.out.flush();
    if (!result)
    {
        report(err, action, input, result.error());
        return SearchResult::failed;
    }
    if (!streams_.out)
    {
        err << "Cannot write the matches\n";
        return SearchResult::failed;
    }
    return searcher.matched() ? SearchResult::matched : SearchResult::not_matched;
}

SearchResult Compressor::search(const fs::path& archive, const std::string& pattern, bool count)
{
    PROFILE_SESSION("Compressor::Search")
    std::ostream& err = streams_.err;
    auto opened = open_archive(err, "search", archive);
    if (!opened)
    {
        return SearchResult::failed;
    }
    const ArchiveInfo& info = opened->info;
    if (!info.search_index)
    {
        err << "Cannot search " << archive.string()
            << ": it has no search index; encode it with --search-index, or use grep\n";
        return SearchResult::failed;
    }
    if (pattern.empty() || pattern.size() > fm_overlap)
    {
        err << "Cannot search for a pattern of " << pattern.size() << " bytes: 1 to " << fm_overlap
            << " are possible\n";
        return SearchResult::failed;
    }
    const auto [section, end] = *info.search_index;
    const auto index = read_search_index(opened->file, section, end, info.total_size);
    const auto found = index ? fm_find(*index, pattern) : std::unexpected(index.error());
    if (!found)
    {
        report(err, "search", archive, {.error = found.error(), .detail = "search index"});
        return SearchResult::failed;
    }

    // Each stored file's data, with every path that shares it; files are back to back in order.
    struct Stored
    {
        uint64_t offset = 0;
        uint64_t size = 0;
        std::vector<std::string> paths;
        std::size_t matches = 0;
    };
    std::vector<Stored> stored;
    for (const Entry& entry : info.index.entries)
    {
        if (entry.type != EntryType::file || entry.size == 0)
        {
            continue;
        }
        const auto same = std::ranges::find_if(stored, [&entry](const Stored& file) {
            return file.offset == entry.offset;
        });
        if (same != stored.end())
        {
            same->paths.push_back(entry.path);
            continue;
        }
        stored.push_back(
            {.offset = entry.offset, .size = entry.size, .paths = {entry.path}, .matches = 0});
    }
    std::ranges::sort(stored, {}, &Stored::offset);

    std::ostream& out = streams_.out;
    bool matched = false;
    for (const uint64_t position : *found)
    {
        auto file = std::ranges::upper_bound(stored, position, {}, &Stored::offset);
        if (file == stored.begin())
        {
            continue;
        }
        --file;
        // Only matches inside one file count; the data runs files together.
        if (position + pattern.size() > file->offset + file->size)
        {
            continue;
        }
        matched = true;
        ++file->matches;
        if (!count)
        {
            for (const std::string& path : file->paths)
            {
                out << path << ':' << (position - file->offset) << '\n';
            }
        }
    }
    if (count)
    {
        for (const Stored& file : stored)
        {
            for (const std::string& path : file.paths)
            {
                if (file.matches > 0)
                {
                    out << path << ':' << file.matches << '\n';
                }
            }
        }
    }
    out.flush();
    if (!out)
    {
        err << "Cannot write the matches\n";
        return SearchResult::failed;
    }
    return matched ? SearchResult::matched : SearchResult::not_matched;
}

} // namespace compressor
