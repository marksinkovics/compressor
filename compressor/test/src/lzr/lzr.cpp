#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include <compressor/deflate/deflate.h>
#include <compressor/error.h>
#include <compressor/lz77/lz77.h>
#include <compressor/lzr/lzr.h>

namespace
{

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

Bytes text(std::size_t size)
{
    static const std::vector<std::string_view> words{"range ",    "coder ", "literal ", "match ",
                                                     "distance ", "slot, ", "state.\n"};
    std::string out;
    uint32_t state = 9;
    while (out.size() < size)
    {
        state = (state * 1103515245U) + 12345U;
        out += words[(state >> 16U) % words.size()];
    }
    out.resize(size);
    return {out.begin(), out.end()};
}

// Records with a random 100 KiB chunk repeated far apart, beyond Deflate's 32 KiB window.
Bytes far_repeats()
{
    const Bytes chunk = noise(100'000, 4);
    Bytes all = chunk;
    const Bytes gap = noise(300'000, 5);
    all.insert(all.end(), gap.begin(), gap.end());
    all.insert(all.end(), chunk.begin(), chunk.end());
    return all;
}

Bytes round_trip(const Bytes& input)
{
    const Bytes coded = compressor::lzr_encode(input, compressor::lz77_options_for_level(7));
    Bytes out{0x5A};
    const auto decoded = compressor::lzr_decode(coded, out, input.size());
    EXPECT_TRUE(decoded) << input.size();
    return {out.begin() + 1, out.end()};
}

} // namespace

TEST(LzrTests, Inputs_of_every_kind_round_trip)
{
    for (const Bytes& input :
         {Bytes{}, Bytes{'a'}, Bytes(10'000, 'q'), text(200'000), noise(50'000, 1), far_repeats()})
    {
        EXPECT_EQ(round_trip(input), input) << input.size();
    }
}

TEST(LzrTests, Matches_reach_across_the_whole_block)
{
    const Bytes input = far_repeats();
    const auto options = compressor::lz77_options_for_level(7);
    const std::size_t lzr = compressor::lzr_encode(input, options).size();
    const std::size_t deflate = compressor::deflate_bytes(input, options).size();
    // The second copy costs almost nothing; Deflate can't reach it.
    EXPECT_LT(lzr, input.size() - 95'000);
    EXPECT_GT(deflate, input.size() - 1000);
}

TEST(LzrTests, Binary_records_come_out_smaller_than_with_deflate)
{
    // 24-byte records: a counter, a field from a few values, and 8 random bytes.
    Bytes input;
    uint32_t state = 3;
    for (uint32_t id = 0; id < 20'000; ++id)
    {
        state = (state * 1103515245U) + 12345U;
        for (unsigned i = 0; i < 4; ++i)
        {
            input.push_back(static_cast<uint8_t>(id >> (8 * i)));
        }
        const uint32_t kind = (state >> 16U) % 5;
        for (unsigned i = 0; i < 12; ++i)
        {
            input.push_back(static_cast<uint8_t>(kind * 37 + i));
        }
        const Bytes random = noise(8, state);
        input.insert(input.end(), random.begin(), random.end());
    }
    const auto options = compressor::lz77_options_for_level(7);
    EXPECT_LT(compressor::lzr_encode(input, options).size(),
              compressor::deflate_bytes(input, options).size());
}

TEST(LzrTests, Damaged_streams_fail_cleanly)
{
    const Bytes input = text(20'000);
    const Bytes coded = compressor::lzr_encode(input, compressor::lz77_options_for_level(7));
    for (std::size_t length = 0; length < coded.size(); length += 61)
    {
        Bytes out;
        EXPECT_FALSE(compressor::lzr_decode(std::span(coded).first(length), out, input.size()))
            << length;
    }
    Bytes out;
    EXPECT_FALSE(compressor::lzr_decode(coded, out, input.size() + 1));
    for (uint32_t seed = 0; seed < 500; ++seed)
    {
        const Bytes garbage = noise(100 + (seed % 400), seed);
        out.clear();
        const auto result = compressor::lzr_decode(garbage, out, seed % 1000);
        EXPECT_TRUE(result || result.error() == compressor::Error::corrupt_payload);
        EXPECT_LE(out.size(), seed % 1000);
    }
}
