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

#include <compressor/error.h>
#include <compressor/stream/decoder.h>
#include <compressor/stream/encoder.h>
#include <compressor/stream/stdio.h>

#include <instrumentor/instrumentor.h>

namespace compressor
{

namespace
{

namespace fs = std::filesystem;

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

template <class Coder>
std::expected<void, Error> write_in_place(Coder& code, std::istream& in, std::ostream& out)
{
    std::expected<void, Error> result = code(in, out);
    out.flush();
    if (result && out.fail())
    {
        result = std::unexpected(Error::write_failed);
    }
    return result;
}

template <class Coder>
bool transcode(const StandardStreams& streams, std::string_view action, const fs::path& input,
               const fs::path& output, Coder code)
{
    std::ostream& err = streams.err;
    std::ifstream file;
    std::istream* in = &streams.in;
    if (!is_standard(input))
    {
        file.open(input, std::ios::in | std::ios::binary);
        if (!file)
        {
            err << "Cannot open input file: " << input.string() << '\n';
            return false;
        }
        in = &file;
    }
    const auto report = [&](Error error) {
        err << "Cannot " << action << ' ' << display_name(input, "standard input") << ": "
            << describe(error) << '\n';
        return false;
    };

    if (is_standard(output))
    {
        if (auto result = write_in_place(code, *in, streams.out); !result)
        {
            return report(result.error());
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
        if (auto result = write_in_place(code, *in, out); !result)
        {
            return report(result.error());
        }
        return true;
    }

    std::optional<TempFile> temp = create_temp_next_to(output);
    if (!temp)
    {
        err << "Cannot open output file: " << output.string() << '\n';
        return false;
    }
    std::expected<void, Error> result = code(*in, temp->stream);
    file.close();
    temp->stream.close();
    if (result && temp->stream.fail())
    {
        result = std::unexpected(Error::write_failed);
    }
    std::error_code error;
    if (!result)
    {
        fs::remove(temp->path, error);
        return report(result.error());
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
    : Compressor(std::make_unique<BlockEncoder>(), std::make_unique<BlockDecoder>())
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
    return transcode(streams_, "encode", input, output,
                     [this](std::istream& in, std::ostream& out) {
        return encoder_->encode(in, out);
    });
}

bool Compressor::decode(const fs::path& input, const fs::path& output)
{
    PROFILE_SESSION("Compressor::Decode")
    return transcode(streams_, "decode", input, output,
                     [this](std::istream& in, std::ostream& out) {
        return decoder_->decode(in, out);
    });
}

} // namespace compressor
