#ifndef Image_h
#define Image_h

#include <cstddef>
#include <cstdint>
#include <expected>
#include <iosfwd>
#include <optional>
#include <span>
#include <vector>

#include <compressor/error.h>

namespace compressor
{

// 8 bits per channel, channels interleaved, rows top to bottom: 1 grey, 2 grey and alpha, 3 RGB,
// 4 RGBA.
struct Image
{
    uint32_t width = 0;
    uint32_t height = 0;
    uint8_t channels = 3;
    std::vector<uint8_t> pixels;

    bool operator==(const Image&) const = default;
};

inline constexpr uint32_t max_image_side = uint32_t{1} << 16U;

// Binary PGM (P5), PPM (P6) and PAM (P7, TUPLTYPE GRAYSCALE, GRAYSCALE_ALPHA, RGB or RGB_ALPHA)
// with a maximum value of 255. not_image for anything else.
std::expected<Image, Error> read_pnm(std::istream& in);
// P5 or P6 for 1 or 3 channels, P7 for 2 or 4.
void write_pnm(std::ostream& out, const Image& image);

// An image file of its own (docs/file_format.md, "Image file"): a reversible colour transform and a
// predictor chosen per strip of rows, and the residuals coded by a context-mixing model. Strips are
// coded on up to `threads` threads; the output doesn't depend on the count.
// Part of an image, in pixels.
struct ImageRegion
{
    uint32_t x = 0;
    uint32_t y = 0;
    uint32_t width = 0;
    uint32_t height = 0;
};

struct ImageDecodeOptions
{
    unsigned threads = 1;
    // Only these pixels (bad_range unless inside the image); only the strips or tiles that cover
    // them are decoded. A lossless file's checksum covers the whole image, so it isn't checked.
    std::optional<ImageRegion> region;
    // A lossy file may be cut short: what it holds of each tile's passes is decoded and the rest
    // of the coefficients left zero, giving a coarser image.
    bool partial = false;
};

std::vector<uint8_t> encode_image(const Image& image, unsigned threads = 1);
std::expected<Image, Error> decode_image(std::span<const uint8_t> bytes,
                                         const ImageDecodeOptions& options = {});
// Whether `bytes` start like an image file.
bool is_image_file(std::span<const uint8_t> bytes);

// A lossy experiment (docs/benchmarks.md, "Image track: lossy experiment"), its own file: 1 or 3
// channels, `quality` 1 to 100 (not_image otherwise). Tiles of 512 x 512 pixels, each in 4 passes
// by frequency band, coded on up to `threads` threads; the output doesn't depend on the count.
std::expected<std::vector<uint8_t>, Error> encode_lossy_image(const Image& image, unsigned quality,
                                                              unsigned threads = 1);
std::expected<Image, Error> decode_lossy_image(std::span<const uint8_t> bytes,
                                               const ImageDecodeOptions& options = {});
bool is_lossy_image_file(std::span<const uint8_t> bytes);

} // namespace compressor

#endif /* Image_h */
