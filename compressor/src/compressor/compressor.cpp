#include <compressor/compressor.h>

#include <cstdio>
#include <expected>
#include <filesystem>
#include <fstream>
#include <ios>
#include <iostream>
#include <istream>
#include <memory>
#include <optional>
#include <ostream>
#include <random>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#include <compressor/archive/archive.h>
#include <compressor/data/data.h>
#include <compressor/engine/engine.h>
#include <compressor/error.h>
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

template <class Coder>
bool transcode(const StandardStreams& streams, std::string_view action, const fs::path& input,
               const fs::path& output, Coder code)
{
    std::ifstream file;
    std::istream* in = &streams.in;
    if (!is_standard(input))
    {
        file.open(input, std::ios::in | std::ios::binary);
        if (!file)
        {
            streams.err << "Cannot open input file: " << input.string() << '\n';
            return false;
        }
        in = &file;
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
    if (is_standard(output))
    {
        err << "Cannot decode " << input.string()
            << ": it is a folder archive; name the new directory with -o\n";
        return false;
    }
    std::error_code error;
    if (fs::exists(fs::symlink_status(output, error)))
    {
        err << "Cannot decode " << input.string()
            << ": the output already exists: " << output.string() << '\n';
        return false;
    }
    const std::optional<fs::path> temp = create_temp_directory_next_to(output);
    if (!temp)
    {
        err << "Cannot create output directory: " << output.string() << '\n';
        return false;
    }
    std::ifstream in(input, std::ios::in | std::ios::binary);
    if (auto result = extract_archive(*decoder_, in, *temp); !result)
    {
        fs::remove_all(*temp, error);
        report(err, "decode", input, result.error());
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

} // namespace compressor
