#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <sstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <compressor/error.h>
#include <compressor/image/image.h>

namespace
{

using compressor::Image;

uint32_t next(uint32_t& state)
{
    state = (state * 1103515245U) + 12345U;
    return state >> 16U;
}

// A smooth gradient with noise, flat boxes and a repeated glyph-like pattern, so every model and
// the copies get used.
Image picture(uint32_t width, uint32_t height, uint8_t channels, uint32_t seed = 1)
{
    Image image{.width = width, .height = height, .channels = channels, .pixels = {}};
    image.pixels.resize(std::size_t{width} * height * channels);
    uint32_t state = seed;
    for (uint32_t y = 0; y < height; ++y)
    {
        for (uint32_t x = 0; x < width; ++x)
        {
            const bool box = (x / 16 + y / 16) % 3 == 0;
            const bool glyph = ((x % 8) * (y % 8)) % 5 == 1;
            for (uint8_t c = 0; c < channels; ++c)
            {
                int v =
                    static_cast<int>((x * (c + 1)) + (y * 2)) + static_cast<int>(next(state) % 7);
                if (box)
                {
                    v = 40 * (c + 1);
                }
                if (glyph)
                {
                    v = 250 - (c * 20);
                }
                image.pixels[((std::size_t{y} * width + x) * channels) + c] =
                    static_cast<uint8_t>(v);
            }
        }
    }
    return image;
}

std::string pnm_of(const Image& image)
{
    std::ostringstream out;
    compressor::write_pnm(out, image);
    return out.str();
}

} // namespace

TEST(ImageTests, Pnm_round_trips_for_every_channel_count)
{
    for (uint8_t channels = 1; channels <= 4; ++channels)
    {
        const Image image = picture(7, 5, channels);
        std::istringstream in(pnm_of(image));
        const auto read = compressor::read_pnm(in);
        ASSERT_TRUE(read) << int{channels};
        EXPECT_EQ(*read, image);
    }
}

TEST(ImageTests, Pnm_headers_with_comments_are_read_and_others_rejected)
{
    std::istringstream commented("P6 # a comment\n2 # width\n1\n255\nabcdef");
    const auto image = compressor::read_pnm(commented);
    ASSERT_TRUE(image);
    EXPECT_EQ(image->width, 2U);
    EXPECT_EQ(image->pixels, (std::vector<uint8_t>{'a', 'b', 'c', 'd', 'e', 'f'}));

    for (const std::string bad :
         {"P3\n1 1\n255\n1 2 3", "P6\n1 1\n65535\nabcdef", "P6\n0 1\n255\n", "P5\n1 1\n255\n",
          "P7\nWIDTH 1\nHEIGHT 1\nDEPTH 3\nMAXVAL 255\nTUPLTYPE CMYK\nENDHDR\nabc",
          "P7\nWIDTH 1\nHEIGHT 1\nDEPTH 5\nMAXVAL 255\nENDHDR\nabcde", "not an image"})
    {
        std::istringstream in(bad);
        EXPECT_FALSE(compressor::read_pnm(in)) << bad;
    }
}

TEST(ImageTests, Images_round_trip_whatever_their_shape_and_channels)
{
    for (uint8_t channels = 1; channels <= 4; ++channels)
    {
        for (const auto& [width, height] : {std::pair{1U, 1U}, std::pair{1U, 9U}, std::pair{9U, 1U},
                                            std::pair{3U, 3U}, std::pair{61U, 47U}})
        {
            const Image image = picture(width, height, channels);
            const std::vector<uint8_t> encoded = compressor::encode_image(image);
            EXPECT_TRUE(compressor::is_image_file(encoded));
            const auto decoded = compressor::decode_image(encoded);
            ASSERT_TRUE(decoded) << int{channels} << ' ' << width << 'x' << height;
            EXPECT_EQ(*decoded, image) << int{channels} << ' ' << width << 'x' << height;
        }
    }
}

TEST(ImageTests, Extreme_values_wrap_back_exactly)
{
    // Black and white next to each other give residuals at both ends of every transform's range.
    Image image{.width = 32, .height = 32, .channels = 3, .pixels = {}};
    uint32_t state = 9;
    for (std::size_t i = 0; i < std::size_t{32} * 32 * 3; ++i)
    {
        image.pixels.push_back(next(state) % 2 == 0 ? 0 : 255);
    }
    const auto decoded = compressor::decode_image(compressor::encode_image(image));
    ASSERT_TRUE(decoded);
    EXPECT_EQ(*decoded, image);
}

TEST(ImageTests, A_large_image_codes_in_strips_the_same_on_any_thread_count)
{
    // Just over 2^19 pixels: two strips.
    const Image image = picture(1100, 500, 1, 3);
    const std::vector<uint8_t> one = compressor::encode_image(image, 1);
    EXPECT_EQ(compressor::encode_image(image, 4), one);
    const auto decoded = compressor::decode_image(one, 4);
    ASSERT_TRUE(decoded);
    EXPECT_EQ(*decoded, image);
}

TEST(ImageTests, Flat_and_repeated_content_codes_small)
{
    Image flat{.width = 200, .height = 100, .channels = 3, .pixels = {}};
    flat.pixels.assign(std::size_t{200} * 100 * 3, 77);
    EXPECT_LT(compressor::encode_image(flat).size(), 100U);

    const Image detailed = picture(128, 128, 3);
    EXPECT_LT(compressor::encode_image(detailed).size(), detailed.pixels.size() * 2 / 3);
}

TEST(ImageTests, Damaged_files_fail_cleanly)
{
    const Image image = picture(40, 30, 3);
    const std::vector<uint8_t> encoded = compressor::encode_image(image);
    EXPECT_EQ(compressor::decode_image(std::span<const uint8_t>()),
              std::unexpected(compressor::Error::not_image));
    for (std::size_t length = 4; length < encoded.size(); length += 7)
    {
        EXPECT_FALSE(compressor::decode_image(std::span(encoded).first(length))) << length;
    }
    std::vector<uint8_t> bad_version = encoded;
    bad_version[4] = 9;
    EXPECT_EQ(compressor::decode_image(bad_version),
              std::unexpected(compressor::Error::unsupported_version));
    std::vector<uint8_t> bad_channels = encoded;
    bad_channels[5] = 5;
    EXPECT_EQ(compressor::decode_image(bad_channels),
              std::unexpected(compressor::Error::corrupt_header));
    uint32_t state = 5;
    for (int round = 0; round < 60; ++round)
    {
        std::vector<uint8_t> flipped = encoded;
        const std::size_t at = 23 + (next(state) % (encoded.size() - 23));
        flipped[at] ^= static_cast<uint8_t>(1U << (next(state) % 8));
        const auto decoded = compressor::decode_image(flipped);
        EXPECT_TRUE(!decoded || *decoded == image) << at;
    }
}
