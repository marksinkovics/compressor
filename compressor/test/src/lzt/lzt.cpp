#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include <compressor/deflate/deflate.h>
#include <compressor/error.h>
#include <compressor/lz77/lz77.h>
#include <compressor/lzt/lzt.h>

namespace
{

using Bytes = std::vector<uint8_t>;

compressor::Lz77Options whole_block(std::size_t size)
{
    compressor::Lz77Options options = compressor::lz77_options_for_level(7);
    options.window = std::max(compressor::lz77_window, size);
    return options;
}

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

Bytes words(std::size_t size)
{
    static constexpr std::array<std::string_view, 6> vocabulary{"sequence ", "literal ", "offset ",
                                                                "match ",    "length ",  "tans\n"};
    std::string text;
    uint32_t state = 11;
    while (text.size() < size)
    {
        state = (state * 1103515245U) + 12345U;
        text += vocabulary[(state >> 16U) % vocabulary.size()];
    }
    text.resize(size);
    return {text.begin(), text.end()};
}

Bytes round_trip(const Bytes& input)
{
    const Bytes coded = compressor::lzt_encode(input, whole_block(input.size()));
    Bytes out;
    EXPECT_TRUE(compressor::lzt_decode(coded, out, input.size()));
    return out;
}

} // namespace

TEST(LztTests, Small_and_odd_inputs_decode_back)
{
    for (const std::string_view text : {"", "a", "ab", "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
                                        "abcabcabcabcabcabcabcabcabcabc", "\xff\x00\xff\x00"})
    {
        const Bytes input(text.begin(), text.end());
        EXPECT_EQ(round_trip(input), input) << text;
    }
    const Bytes runs(300'000, 'r'); // matches of 258 at distance 1, then the repeat offset
    EXPECT_EQ(round_trip(runs), runs);
    EXPECT_LT(compressor::lzt_encode(runs, whole_block(runs.size())).size(), 2000U);
}

TEST(LztTests, Far_repeats_beat_deflates_window)
{
    // A 100 KB chunk of noise twice: Deflate's 32 KiB can't reach back, LZT's window can.
    const Bytes chunk = noise(100'000, 3);
    Bytes input = chunk;
    input.insert(input.end(), chunk.begin(), chunk.end());
    const Bytes coded = compressor::lzt_encode(input, whole_block(input.size()));
    EXPECT_LT(coded.size(), chunk.size() + 2'000);
    EXPECT_GT(compressor::deflate_bytes(input).size(), input.size() * 9 / 10);
    EXPECT_EQ(round_trip(input), input);
}

TEST(LztTests, Text_and_noise_decode_back)
{
    const Bytes text = words(500'000);
    EXPECT_LT(compressor::lzt_encode(text, whole_block(text.size())).size(), text.size() / 8);
    EXPECT_EQ(round_trip(text), text);
    const Bytes random = noise(70'000, 9);
    EXPECT_EQ(round_trip(random), random);
}

TEST(LztTests, Decoding_appends_to_what_is_there)
{
    const Bytes input = words(5000);
    Bytes out{1, 2, 3};
    ASSERT_TRUE(compressor::lzt_decode(compressor::lzt_encode(input, whole_block(input.size())),
                                       out, input.size()));
    EXPECT_EQ(Bytes(out.begin(), out.begin() + 3), Bytes({1, 2, 3}));
    EXPECT_EQ(Bytes(out.begin() + 3, out.end()), input);
}

TEST(LztTests, Damaged_payloads_fail_and_leave_the_output_alone)
{
    const Bytes input = words(20'000);
    const Bytes coded = compressor::lzt_encode(input, whole_block(input.size()));
    const auto fails = [&](const Bytes& payload, std::size_t size) {
        Bytes out{7};
        const auto result = compressor::lzt_decode(payload, out, size);
        EXPECT_EQ(out, Bytes{7});
        return !result && result.error() == compressor::Error::corrupt_payload;
    };
    EXPECT_TRUE(fails({}, 1));
    for (std::size_t cut = 0; cut < coded.size(); cut += 97)
    {
        EXPECT_TRUE(fails(Bytes(coded.begin(), coded.begin() + static_cast<std::ptrdiff_t>(cut)),
                          input.size()))
            << cut;
    }
    Bytes extra = coded;
    extra.push_back(1);
    EXPECT_TRUE(fails(extra, input.size()));
    EXPECT_TRUE(fails(coded, input.size() + 1));
    EXPECT_TRUE(fails(coded, input.size() - 1));
    Bytes counts = coded;
    counts[0] = static_cast<uint8_t>(counts[0] + 1); // one sequence more than coded
    EXPECT_TRUE(fails(counts, input.size()));
}
