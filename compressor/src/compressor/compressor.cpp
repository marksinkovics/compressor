#include <compressor/compressor.h>

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
#include <compressor/engine/engine.h>
#include <compressor/error.h>
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

std::unique_ptr<IEngine> make_engine()
{
    return std::make_unique<Engine>();
}

unsigned thread_count(unsigned requested)
{
    return requested == 0 ? default_thread_count() : requested;
}

struct TempFile
{
    fs::path path;
    std::ofstream stream;
};

// Retries only when the random name is taken, not when the directory can't be written.
std::optional<TempFile> create_temp_next_to(const fs::path& output)
{
    std::random_device random;
    for (int attempt = 0; attempt < 8; ++attempt)
    {
        std::ostringstream name;
        name << '.' << output.filename().string() << '.' << std::hex << random() << ".tmp";
        TempFile temp{.path = output.parent_path() / name.str(), .stream = {}};
        temp.stream.open(temp.path, std::ios::out | std::ios::binary | std::ios::noreplace);
        if (temp.stream)
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
    Result result = write(temp->stream);
    temp->stream.close();
    if (result && temp->stream.fail())
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
    : Compressor(0)
{
}

Compressor::Compressor(unsigned threads)
    : Compressor(std::make_unique<BlockEncoder>(make_engine, default_block_size_log2,
                                                thread_count(threads)),
                 std::make_unique<BlockDecoder>(make_engine, thread_count(threads)))
{
}

Compressor::Compressor(std::unique_ptr<IBlockEncoder> encoder,
                       std::unique_ptr<IBlockDecoder> decoder, StandardStreams streams)
    : encoder_(std::move(encoder))
    , decoder_(std::move(decoder))
    , streams_(streams)
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
        return write_archive(*encoder_, input, std::move(*entries), out);
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

} // namespace compressor
