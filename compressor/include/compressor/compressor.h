#ifndef Compressor_h
#define Compressor_h

#include <filesystem>
#include <iostream>
#include <memory>

#include <compressor/stream/decoder.h>
#include <compressor/stream/encoder.h>

namespace compressor
{

class ICompressor
{
public:
    virtual ~ICompressor() = default;
    virtual bool encode(const std::filesystem::path& input,
                        const std::filesystem::path& output) = 0;
    virtual bool decode(const std::filesystem::path& input,
                        const std::filesystem::path& output) = 0;
};

// Writes a regular output through a temporary file next to it and renames that on success, so a
// failure leaves an existing output untouched and encoding a file onto itself works. Devices and
// pipes such as /dev/null are written in place.
class Compressor : public ICompressor
{
public:
    Compressor();
    Compressor(std::unique_ptr<IBlockEncoder> encoder, std::unique_ptr<IBlockDecoder> decoder,
               std::ostream& err = std::cerr);
    ~Compressor() override = default;

    bool encode(const std::filesystem::path& input, const std::filesystem::path& output) override;
    bool decode(const std::filesystem::path& input, const std::filesystem::path& output) override;

private:
    std::unique_ptr<IBlockEncoder> encoder_;
    std::unique_ptr<IBlockDecoder> decoder_;
    std::ostream& err_;
};

} // namespace compressor

#endif /* Compressor_h */
