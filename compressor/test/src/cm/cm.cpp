#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include <compressor/bwt/bwt.h>
#include <compressor/cm/cm.h>
#include <compressor/error.h>

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
    static const std::vector<std::string_view> words{"context ", "mixing ",  "predicts ", "bits ",
                                                     "from ",    "orders, ", "words.\n"};
    std::string out;
    uint32_t state = 3;
    while (out.size() < size)
    {
        state = (state * 1103515245U) + 12345U;
        out += words[(state >> 16U) % words.size()];
    }
    out.resize(size);
    return {out.begin(), out.end()};
}

Bytes round_trip(const Bytes& input, unsigned table_log2 = compressor::cm_default_table_log2)
{
    const Bytes coded = compressor::cm_encode(input, table_log2);
    Bytes decoded;
    EXPECT_TRUE(compressor::cm_decode(coded, decoded, input.size()));
    return decoded;
}

} // namespace

TEST(CmTests, Small_inputs_decode_back)
{
    for (const std::string_view input : {"", "a", "ab", "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
                                         "abcabcabcabcabcabcabcabcabc", "\xff\x00\xff"})
    {
        const Bytes bytes(input.begin(), input.end());
        EXPECT_EQ(round_trip(bytes), bytes) << input;
    }
}

TEST(CmTests, Text_beats_the_bwt_coder)
{
    const Bytes input = text(300'000);
    const Bytes coded = compressor::cm_encode(input);
    EXPECT_LT(coded.size(), compressor::bwt_encode(input).size());
    Bytes decoded;
    ASSERT_TRUE(compressor::cm_decode(coded, decoded, input.size()));
    EXPECT_EQ(decoded, input);
}

TEST(CmTests, Noise_grows_only_slightly)
{
    const Bytes input = noise(100'000, 1);
    EXPECT_LT(compressor::cm_encode(input).size(), input.size() + (input.size() / 100));
    EXPECT_EQ(round_trip(input), input);
}

TEST(CmTests, The_match_model_finds_a_far_repeat)
{
    const Bytes chunk = noise(50'000, 4);
    Bytes input = chunk;
    const Bytes gap = noise(200'000, 5);
    input.insert(input.end(), gap.begin(), gap.end());
    input.insert(input.end(), chunk.begin(), chunk.end());
    const Bytes coded = compressor::cm_encode(input);
    EXPECT_LT(coded.size(), chunk.size() + gap.size() + 2'000);
    EXPECT_EQ(round_trip(input), input);
}

TEST(CmTests, Every_table_size_decodes_back)
{
    const Bytes input = text(40'000);
    for (unsigned log2 = compressor::cm_min_table_log2; log2 <= compressor::cm_max_table_log2;
         log2 += 5)
    {
        EXPECT_EQ(round_trip(input, log2), input) << log2;
    }
}

TEST(CmTests, Decoding_appends_to_what_is_there)
{
    const Bytes input = text(5000);
    const Bytes coded = compressor::cm_encode(input);
    Bytes out{1, 2, 3};
    ASSERT_TRUE(compressor::cm_decode(coded, out, input.size()));
    ASSERT_EQ(out.size(), input.size() + 3);
    EXPECT_EQ(Bytes(out.begin() + 3, out.end()), input);
}

TEST(CmTests, Damaged_payloads_fail)
{
    const Bytes input = text(20'000);
    const Bytes coded = compressor::cm_encode(input);
    Bytes out;
    EXPECT_EQ(compressor::cm_decode({}, out, 1).error(), compressor::Error::corrupt_payload);
    Bytes bad_size = coded;
    bad_size[0] = 99;
    EXPECT_EQ(compressor::cm_decode(bad_size, out, input.size()).error(),
              compressor::Error::corrupt_payload);
    const Bytes short_payload(coded.begin(), coded.end() - 10);
    EXPECT_EQ(compressor::cm_decode(short_payload, out, input.size()).error(),
              compressor::Error::corrupt_payload);
    Bytes long_payload = coded;
    long_payload.push_back(0);
    EXPECT_EQ(compressor::cm_decode(long_payload, out, input.size()).error(),
              compressor::Error::corrupt_payload);
    EXPECT_TRUE(out.empty());
}
