#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include <compressor/transform/transform.h>

namespace
{

using compressor::Transform;
using compressor::TransformId;
using Bytes = std::vector<uint8_t>;

Bytes noise(std::size_t size, uint32_t seed)
{
    Bytes bytes(size);
    for (uint8_t& byte : bytes)
    {
        seed = (seed * 1103515245U) + 12345U;
        byte = static_cast<uint8_t>(seed >> 16U);
    }
    return bytes;
}

void put_le32(Bytes& bytes, std::size_t at, uint32_t value)
{
    for (unsigned i = 0; i < 4; ++i)
    {
        bytes[at + i] = static_cast<uint8_t>(value >> (8 * i));
    }
}

// ARM64-like code: words with BL calls to a few functions among other instructions.
Bytes arm64_code(std::size_t words)
{
    Bytes code = noise(words * 4, 3);
    for (std::size_t word = 0; word < words; word += 8)
    {
        const auto target = static_cast<uint32_t>((word % 5) * 1000);
        put_le32(code, word * 4,
                 0x94000000U | ((target - static_cast<uint32_t>(word)) & 0x03FFFFFFU));
    }
    return code;
}

// A textured RGBA image whose alpha (the top byte of each little-endian word) looks like a BL
// opcode in every word, with `row` bytes per row (padding included).
Bytes rgba_image(std::size_t width, std::size_t height, std::size_t row)
{
    Bytes image(row * height, 0);
    uint32_t seed = 11;
    for (std::size_t y = 0; y < height; ++y)
    {
        for (std::size_t x = 0; x < width; ++x)
        {
            seed = (seed * 1103515245U) + 12345U;
            const auto grain = static_cast<int>((seed >> 16U) % 9);
            uint8_t* pixel = image.data() + (y * row) + (x * 4);
            pixel[0] = static_cast<uint8_t>((x * 3) + (y * 2) + grain);
            pixel[1] = static_cast<uint8_t>(((x * y) / 64) + grain);
            pixel[2] = static_cast<uint8_t>(200 - y + grain);
            pixel[3] = 0x96;
        }
    }
    return image;
}

} // namespace

TEST(TransformTests, Every_transform_undoes_exactly_on_any_bytes)
{
    const std::vector<Transform> transforms{
        {.id = TransformId::delta, .parameter = 1},
        {.id = TransformId::delta, .parameter = 3},
        {.id = TransformId::delta, .parameter = 4096},
        {.id = TransformId::arm64_branch, .parameter = 0},
        {.id = TransformId::x86_branch, .parameter = 0},
        {.id = TransformId::med, .parameter = compressor::image_parameter(2, 1)},
        {.id = TransformId::med, .parameter = compressor::image_parameter(7, 3)},
        {.id = TransformId::med, .parameter = compressor::image_parameter(4096, 16)}};
    for (const std::size_t size :
         {std::size_t{0}, std::size_t{3}, std::size_t{4}, std::size_t{5}, std::size_t{100'003}})
    {
        for (const Transform& transform : transforms)
        {
            Bytes bytes = noise(size, static_cast<uint32_t>(size));
            // Plenty of E8 bytes and BL words, including back to back.
            for (std::size_t i = 0; i + 8 < bytes.size(); i += 13)
            {
                bytes[i] = 0xE8;
                bytes[i + 4] = 0x94;
            }
            const Bytes original = bytes;
            compressor::apply_transform(transform, bytes);
            compressor::undo_transform(transform, bytes);
            EXPECT_EQ(bytes, original) << static_cast<int>(transform.id) << ' ' << size;
        }
    }
}

TEST(TransformTests, Arm64_calls_to_one_function_become_the_same_bytes)
{
    Bytes code(64);
    put_le32(code, 0, 0x94000000U | 10U);                         // at word 0, to word 10
    put_le32(code, 20, 0x94000000U | ((10U - 5U) & 0x03FFFFFFU)); // at word 5, to word 10
    compressor::apply_transform({.id = TransformId::arm64_branch, .parameter = 0}, code);
    EXPECT_TRUE(std::equal(code.begin(), code.begin() + 4, code.begin() + 20));
}

TEST(TransformTests, Delta_turns_a_ramp_into_ones)
{
    Bytes ramp(10);
    for (std::size_t i = 0; i < ramp.size(); ++i)
    {
        ramp[i] = static_cast<uint8_t>(i * 2);
    }
    compressor::apply_transform({.id = TransformId::delta, .parameter = 1}, ramp);
    EXPECT_EQ(ramp, (Bytes{0, 2, 2, 2, 2, 2, 2, 2, 2, 2}));
}

TEST(TransformTests, The_choice_follows_the_data)
{
    // A random walk in 16-bit samples: nothing repeats a row apart.
    Bytes samples;
    uint32_t state = 5;
    int32_t value = 20'000;
    for (int i = 0; i < 40'000; ++i)
    {
        state = (state * 1103515245U) + 12345U;
        value += static_cast<int32_t>((state >> 16U) % 9) - 4;
        samples.push_back(static_cast<uint8_t>(static_cast<uint16_t>(value)));
        samples.push_back(static_cast<uint8_t>(static_cast<uint16_t>(value) >> 8U));
    }
    const auto delta = compressor::choose_transforms(samples);
    ASSERT_EQ(delta.size(), 1U);
    EXPECT_EQ(delta[0].id, TransformId::delta);
    EXPECT_EQ(delta[0].parameter, 2U);

    EXPECT_EQ(compressor::choose_transforms(arm64_code(20'000)),
              (std::vector<Transform>{{.id = TransformId::arm64_branch, .parameter = 0}}));

    Bytes text;
    for (std::size_t i = 0; i < 2000; ++i)
    {
        for (const char c : std::string_view("words in a sentence, "))
        {
            text.push_back(static_cast<uint8_t>(c));
        }
    }
    EXPECT_TRUE(compressor::choose_transforms(text).empty());
    EXPECT_TRUE(compressor::choose_transforms(noise(100'000, 9)).empty());
}

TEST(TransformTests, An_image_takes_the_median_predictor_with_its_row_length)
{
    // 301 pixels of 4 bytes and 8 bytes of padding per row.
    const Bytes image = rgba_image(301, 120, 1212);
    const auto chosen = compressor::choose_transforms(image);
    ASSERT_EQ(chosen.size(), 1U);
    EXPECT_EQ(chosen[0].id, TransformId::med);
    EXPECT_EQ(chosen[0].parameter >> 24U, 4U);
    const uint32_t row = chosen[0].parameter & 0xFFFFFFU;
    EXPECT_GE(row, 1212U - 4);
    EXPECT_LE(row, 1212U + 4);

    // The alpha bytes look like calls; they come after the predictors, not first.
    const auto thorough = compressor::choose_transforms(image, true);
    ASSERT_FALSE(thorough.empty());
    EXPECT_EQ(thorough[0].id, TransformId::med);

    Bytes residuals = image;
    compressor::apply_transform(chosen[0], residuals);
    const auto small = std::ranges::count_if(residuals, [](uint8_t r) {
        return r < 16 || r > 240;
    });
    EXPECT_GT(small, static_cast<std::ptrdiff_t>(residuals.size() * 9 / 10));
}

TEST(TransformTests, Only_known_ids_and_parameters_parse)
{
    using compressor::parse_transform;
    EXPECT_EQ(parse_transform(1, 1), (Transform{.id = TransformId::delta, .parameter = 1}));
    EXPECT_FALSE(parse_transform(1, 0));
    EXPECT_FALSE(parse_transform(1, 65'536));
    EXPECT_EQ(parse_transform(3, 0), (Transform{.id = TransformId::arm64_branch, .parameter = 0}));
    EXPECT_FALSE(parse_transform(3, 1));
    EXPECT_EQ(parse_transform(4, 0), (Transform{.id = TransformId::x86_branch, .parameter = 0}));
    const uint32_t image = compressor::image_parameter(900, 3);
    EXPECT_EQ(parse_transform(5, image), (Transform{.id = TransformId::med, .parameter = image}));
    EXPECT_FALSE(parse_transform(5, compressor::image_parameter(900, 0)));
    EXPECT_FALSE(parse_transform(5, compressor::image_parameter(900, 17)));
    EXPECT_FALSE(parse_transform(5, compressor::image_parameter(3, 3)));
    EXPECT_FALSE(parse_transform(2, 0));
    EXPECT_FALSE(parse_transform(6, 0));
    EXPECT_FALSE(parse_transform(0, 0));
}
