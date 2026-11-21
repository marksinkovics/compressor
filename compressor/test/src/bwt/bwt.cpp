#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <numeric>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include <compressor/bwt/bwt.h>
#include <compressor/deflate/deflate.h>
#include <compressor/error.h>
#include <compressor/lz77/lz77.h>

namespace
{

using Bytes = std::vector<uint8_t>;

Bytes bytes_of(std::string_view text)
{
    return {text.begin(), text.end()};
}

Bytes noise(std::size_t size, uint32_t seed, unsigned alphabet = 256)
{
    Bytes bytes(size);
    for (uint8_t& byte : bytes)
    {
        seed = (seed * 1103515245U) + 12345U;
        byte = static_cast<uint8_t>((seed >> 16U) % alphabet);
    }
    return bytes;
}

Bytes prose(std::size_t size)
{
    static const std::vector<std::string_view> words{
        "the ",    "compressor ", "sorts ", "every ", "suffix ", "of ",     "a ",
        "block, ", "then ",       "moves ", "bytes ", "to ",     "front.\n"};
    std::string text;
    uint32_t state = 11;
    while (text.size() < size)
    {
        state = (state * 1103515245U) + 12345U;
        text += words[(state >> 16U) % words.size()];
    }
    text.resize(size);
    return bytes_of(text);
}

std::vector<Bytes> samples()
{
    return {Bytes{},          bytes_of("a"),     bytes_of("banana"), bytes_of("abracadabra"),
            Bytes(5000, 'z'), noise(3000, 1, 2), noise(10'000, 2),   prose(100'000)};
}

} // namespace

TEST(BwtTests, The_suffix_array_matches_a_plain_sort)
{
    for (const Bytes& input : samples())
    {
        std::vector<uint32_t> expected(input.size());
        std::ranges::iota(expected, 0U);
        std::ranges::sort(expected, [&](uint32_t a, uint32_t b) {
            return std::lexicographical_compare(input.begin() + a, input.end(), input.begin() + b,
                                                input.end());
        });
        EXPECT_EQ(compressor::suffix_array(input), expected) << input.size();
    }
}

TEST(BwtTests, Banana_transforms_as_by_hand)
{
    // Rows of "banana$": $banana, a$banan, ana$ban, anana$b, banana$, na$bana, nana$ba.
    Bytes out;
    const uint32_t primary = compressor::forward_bwt(bytes_of("banana"), out);
    EXPECT_EQ(std::string(out.begin(), out.end()), "annbaa");
    EXPECT_EQ(primary, 4U);
}

TEST(BwtTests, Every_sample_round_trips_through_the_transform_and_the_coder)
{
    for (const Bytes& input : samples())
    {
        Bytes transformed;
        const uint32_t primary = compressor::forward_bwt(input, transformed);
        Bytes restored{0x55};
        ASSERT_TRUE(compressor::inverse_bwt(transformed, primary, restored)) << input.size();
        EXPECT_EQ(Bytes(restored.begin() + 1, restored.end()), input);

        Bytes decoded;
        ASSERT_TRUE(compressor::bwt_decode(compressor::bwt_encode(input), decoded, input.size()))
            << input.size();
        EXPECT_EQ(decoded, input);
    }
}

TEST(BwtTests, Text_comes_out_smaller_than_with_lz77)
{
    const Bytes text = prose(500'000);
    const std::size_t bwt = compressor::bwt_encode(text).size();
    const std::size_t lz77 =
        compressor::deflate_bytes(text, compressor::lz77_options_for_level(9)).size();
    EXPECT_LT(bwt, lz77);
}

TEST(BwtTests, Damaged_payloads_fail_cleanly)
{
    const Bytes input = prose(20'000);
    const Bytes payload = compressor::bwt_encode(input);
    Bytes out;
    EXPECT_EQ(compressor::bwt_decode(payload, out, input.size() - 1),
              std::unexpected(compressor::Error::corrupt_payload));
    Bytes bad_primary = payload;
    bad_primary[0] = 0xFF;
    bad_primary[1] = 0xFF;
    out.clear();
    EXPECT_FALSE(compressor::bwt_decode(bad_primary, out, input.size()));
    for (std::size_t length = 0; length < payload.size(); length += 97)
    {
        out.clear();
        EXPECT_FALSE(compressor::bwt_decode(std::span(payload).first(length), out, input.size()))
            << length;
    }
    for (uint32_t seed = 0; seed < 500; ++seed)
    {
        const Bytes garbage = noise(200 + (seed % 300), seed);
        out.clear();
        const auto result = compressor::bwt_decode(garbage, out, seed % 400);
        EXPECT_TRUE(result || result.error() == compressor::Error::corrupt_payload);
    }
}

TEST(BwtTests, The_suffix_array_matches_a_plain_sort_where_sa_is_recurses)
{
    // SA-IS against sorting the suffixes directly, on inputs that exercise its recursion: runs,
    // periodic strings, few symbols, all symbols.
    std::vector<std::vector<uint8_t>> inputs;
    for (const std::string_view text :
         {"", "a", "aa", "ab", "ba", "banana", "mississippi", "abracadabra", "aaaaaaaaaaaab",
          "baaaaaaaaaaaa", "abababababababab", "abcabcabcabcabd"})
    {
        inputs.emplace_back(text.begin(), text.end());
    }
    uint32_t state = 5;
    for (const std::size_t size : {std::size_t{100}, std::size_t{1000}, std::size_t{5000}})
    {
        for (const unsigned alphabet : {2U, 4U, 256U})
        {
            std::vector<uint8_t> bytes(size);
            for (uint8_t& byte : bytes)
            {
                state = (state * 1103515245U) + 12345U;
                byte = static_cast<uint8_t>((state >> 16U) % alphabet);
            }
            inputs.push_back(bytes);
        }
    }
    for (const auto& bytes : inputs)
    {
        std::vector<uint32_t> expected(bytes.size());
        std::ranges::iota(expected, 0U);
        std::ranges::sort(expected, [&](uint32_t a, uint32_t b) {
            return std::ranges::lexicographical_compare(std::span(bytes).subspan(a),
                                                        std::span(bytes).subspan(b));
        });
        EXPECT_EQ(compressor::suffix_array(bytes), expected) << bytes.size();
    }
}
