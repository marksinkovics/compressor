#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <span>
#include <sstream>
#include <string>

#include <compressor/image/image.h>

// NOLINTNEXTLINE(readability-identifier-naming): libFuzzer's entry point
extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, std::size_t size)
{
    const std::span<const uint8_t> bytes(data, size);
    // The decoder allows up to 32768 pixels per input byte; fuzzing keeps to what fits its memory
    // limit.
    if (size >= 15)
    {
        const uint64_t width = bytes[7] | (uint64_t{bytes[8]} << 8U) | (uint64_t{bytes[9]} << 16U) |
                               (uint64_t{bytes[10]} << 24U);
        const uint64_t height = bytes[11] | (uint64_t{bytes[12]} << 8U) |
                                (uint64_t{bytes[13]} << 16U) | (uint64_t{bytes[14]} << 24U);
        if (width * height > (uint64_t{1} << 22U))
        {
            return 0;
        }
    }
    const auto image = compressor::decode_image(bytes);
    // What decodes must encode and decode to the same pixels.
    if (image && compressor::decode_image(compressor::encode_image(*image)) != image)
    {
        std::abort();
    }
    std::istringstream in(std::string(reinterpret_cast<const char*>(data), size));
    (void)compressor::read_pnm(in).has_value();
    return 0;
}
