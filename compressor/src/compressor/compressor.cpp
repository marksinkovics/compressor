#include <compressor/compressor.h>

#include <expected>
#include <filesystem>
#include <fstream>
#include <ios>
#include <istream>
#include <memory>
#include <optional>
#include <ostream>
#include <random>
#include <sstream>
#include <string_view>
#include <system_error>
#include <utility>

#include <compressor/error.h>
#include <compressor/stream/decoder.h>
#include <compressor/stream/encoder.h>

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

template <class Coder>
bool transcode(std::ostream& err, std::string_view action, const fs::path& input,
               const fs::path& output, Coder code)
{
    std::ifstream in(input, std::ios::in | std::ios::binary);
    if (!in)
    {
        err << "Cannot open input file: " << input.string() << '\n';
        return false;
    }
    const auto report = [&](Error error) {
        err << "Cannot " << action << ' ' << input.string() << ": " << describe(error) << '\n';
        return false;
    };

    if (!replaces_output(output))
    {
        std::ofstream out(output, std::ios::out | std::ios::binary);
        if (!out)
        {
            err << "Cannot open output file: " << output.string() << '\n';
            return false;
        }
        if (auto result = code(in, out); !result)
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
    std::expected<void, Error> result = code(in, temp->stream);
    in.close();
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

Compressor::Compressor()
    : Compressor(std::make_unique<BlockEncoder>(), std::make_unique<BlockDecoder>())
{
}

Compressor::Compressor(std::unique_ptr<IBlockEncoder> encoder,
                       std::unique_ptr<IBlockDecoder> decoder, std::ostream& err)
    : encoder_(std::move(encoder))
    , decoder_(std::move(decoder))
    , err_(err)
{
}

bool Compressor::encode(const fs::path& input, const fs::path& output)
{
    PROFILE_SESSION("Compressor::Encode")
    return transcode(err_, "encode", input, output, [this](std::istream& in, std::ostream& out) {
        return encoder_->encode(in, out);
    });
}

bool Compressor::decode(const fs::path& input, const fs::path& output)
{
    PROFILE_SESSION("Compressor::Decode")
    return transcode(err_, "decode", input, output, [this](std::istream& in, std::ostream& out) {
        return decoder_->decode(in, out);
    });
}

} // namespace compressor
