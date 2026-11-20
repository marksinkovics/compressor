#ifndef Compressor_h
#define Compressor_h

#include <filesystem>
#include <iosfwd>
#include <memory>
#include <string>
#include <string_view>

#include <compressor/stream/decoder.h>
#include <compressor/stream/encoder.h>

namespace compressor
{

// As an input or output path, the standard input or output; "./-" names a file called "-".
inline constexpr std::string_view standard_stream = "-";

struct StandardStreams
{
    std::istream& in;
    std::ostream& out;
    std::ostream& err;
};

// stdin and stdout through StdioInputBuf and StdioOutputBuf, and std::cerr.
StandardStreams process_streams();

class ICompressor
{
public:
    virtual ~ICompressor() = default;
    virtual bool encode(const std::filesystem::path& input,
                        const std::filesystem::path& output) = 0;
    virtual bool decode(const std::filesystem::path& input,
                        const std::filesystem::path& output) = 0;
    // Prints the paths in a folder archive to the standard output; `details` adds the types,
    // permissions, sizes and times.
    virtual bool list(const std::filesystem::path& archive, bool details) = 0;
    // Writes the file, directory or symlink at `path` in a folder archive to `output`, which must
    // not exist unless `path` is a file; only a file can go to the standard output.
    virtual bool extract(const std::filesystem::path& archive, const std::string& path,
                         const std::filesystem::path& output) = 0;
    // Decodes everything and checks every checksum, writing nothing but a line on success.
    virtual bool verify(const std::filesystem::path& input) = 0;
};

// Writes a regular output through a temporary file next to it and renames that on success, so a
// failure leaves an existing output untouched and encoding a file onto itself works. Devices and
// pipes such as /dev/null are written in place, and so is the standard output, where a failed
// decode may leave the blocks before the failure. A directory input is encoded as a folder
// archive, and an archive file decodes into a new directory, built next to it and renamed.
class Compressor : public ICompressor
{
public:
    // Block coders with default_thread_count() threads.
    Compressor();
    // 0 means default_thread_count().
    explicit Compressor(unsigned threads);
    Compressor(std::unique_ptr<IBlockEncoder> encoder, std::unique_ptr<IBlockDecoder> decoder,
               StandardStreams streams = process_streams());
    ~Compressor() override = default;

    bool encode(const std::filesystem::path& input, const std::filesystem::path& output) override;
    bool decode(const std::filesystem::path& input, const std::filesystem::path& output) override;
    bool list(const std::filesystem::path& archive, bool details) override;
    bool extract(const std::filesystem::path& archive, const std::string& path,
                 const std::filesystem::path& output) override;
    bool verify(const std::filesystem::path& input) override;

private:
    bool encode_folder(const std::filesystem::path& input, const std::filesystem::path& output);
    bool decode_folder(const std::filesystem::path& input, const std::filesystem::path& output);

    std::unique_ptr<IBlockEncoder> encoder_;
    std::unique_ptr<IBlockDecoder> decoder_;
    StandardStreams streams_;
};

} // namespace compressor

#endif /* Compressor_h */
