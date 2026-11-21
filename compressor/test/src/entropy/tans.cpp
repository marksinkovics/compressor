#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include <compressor/entropy/rans.h>
#include <compressor/entropy/tans.h>
#include <compressor/error.h>

namespace
{

using Bytes = std::vector<uint8_t>;

// Byte 0 with probability `p0`, the rest spread over 1..`alphabet`-1.
Bytes skewed(std::size_t count, double p0, std::size_t alphabet, uint32_t seed = 1)
{
    Bytes bytes(count);
    for (uint8_t& byte : bytes)
    {
        seed = (seed * 1103515245U) + 12345U;
        const double u = static_cast<double>(seed >> 8U) / static_cast<double>(1U << 24U);
        seed = (seed * 1103515245U) + 12345U;
        byte = u < p0 ? 0 : static_cast<uint8_t>(1 + ((seed >> 16U) % (alphabet - 1)));
    }
    return bytes;
}

double entropy_bytes(const Bytes& bytes)
{
    std::vector<double> counts(256);
    for (const uint8_t byte : bytes)
    {
        ++counts[byte];
    }
    double bits = 0;
    for (const double count : counts)
    {
        if (count > 0)
        {
            bits -= count * std::log2(count / static_cast<double>(bytes.size()));
        }
    }
    return bits / 8;
}

Bytes encode(const Bytes& bytes)
{
    Bytes out;
    compressor::tans_encode(bytes, out);
    return out;
}

Bytes round_trip(const Bytes& bytes)
{
    Bytes decoded;
    EXPECT_TRUE(compressor::tans_decode(encode(bytes), bytes.size(), decoded));
    return decoded;
}

} // namespace

TEST(TansTests, Small_inputs_and_single_bytes_decode_back)
{
    for (const std::string_view input : {"", "a", "ab", "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
                                         "abcabcabcabcabcabcabcabcabc", "\xff\x00\xff"})
    {
        const Bytes bytes(input.begin(), input.end());
        EXPECT_EQ(round_trip(bytes), bytes) << input;
    }
    const Bytes same(100'000, 'x');
    EXPECT_EQ(round_trip(same), same);
    EXPECT_LT(encode(same).size(), 300U); // the table, and no bits per byte
}

TEST(TansTests, Codes_close_to_the_entropy_and_to_rans)
{
    for (const double p0 : {0.05, 0.5, 0.9, 0.99})
    {
        const Bytes bytes = skewed(200'000, p0, 40);
        const Bytes coded = encode(bytes);
        // Within 1%, or a few states' worth where 2^11 states round a skewed byte's share.
        EXPECT_LT(static_cast<double>(coded.size()), (entropy_bytes(bytes) * 1.01) + 400) << p0;
        std::vector<uint8_t> rans;
        compressor::rans_encode(std::vector<uint16_t>(bytes.begin(), bytes.end()), 256, rans);
        EXPECT_LT(static_cast<double>(coded.size()),
                  (static_cast<double>(rans.size()) * 1.01) + 100)
            << p0;
        EXPECT_EQ(round_trip(bytes), bytes) << p0;
    }
}

TEST(TansTests, Every_byte_value_decodes_back)
{
    Bytes bytes = skewed(70'000, 0.3, 256, 9);
    for (int i = 0; i < 256; ++i)
    {
        bytes.push_back(static_cast<uint8_t>(i));
    }
    EXPECT_EQ(round_trip(bytes), bytes);
}

TEST(TansTests, Decoding_appends_to_what_is_there)
{
    const Bytes bytes = skewed(5000, 0.7, 10);
    Bytes out{1, 2, 3};
    ASSERT_TRUE(compressor::tans_decode(encode(bytes), bytes.size(), out));
    EXPECT_EQ(Bytes(out.begin(), out.begin() + 3), Bytes({1, 2, 3}));
    EXPECT_EQ(Bytes(out.begin() + 3, out.end()), bytes);
}

TEST(TansTests, Damaged_streams_fail_and_leave_the_output_alone)
{
    const Bytes bytes = skewed(20'000, 0.6, 30);
    const Bytes coded = encode(bytes);
    const auto fails = [&](const Bytes& damaged, std::size_t count) {
        Bytes out{7};
        const auto result = compressor::tans_decode(damaged, count, out);
        EXPECT_EQ(out, Bytes{7});
        return !result && result.error() == compressor::Error::corrupt_payload;
    };
    EXPECT_TRUE(fails({}, 1));
    EXPECT_TRUE(fails(Bytes(coded.begin(), coded.end() - 1), bytes.size()));
    Bytes extra = coded;
    extra.push_back(1);
    EXPECT_TRUE(fails(extra, bytes.size()));
    EXPECT_TRUE(fails(coded, bytes.size() + 1));
    EXPECT_TRUE(fails(coded, bytes.size() - 1));
    Bytes bad_table = coded;
    bad_table[0] = static_cast<uint8_t>(bad_table[0] + 1);
    EXPECT_TRUE(fails(bad_table, bytes.size()));
    EXPECT_TRUE(fails(encode({}), 1));
    EXPECT_TRUE(fails(coded, 0));
}

TEST(TansTests, The_compact_table_lists_only_used_bytes)
{
    const Bytes bytes = skewed(50'000, 0.7, 12);
    Bytes full;
    Bytes compact;
    compressor::tans_encode(bytes, full);
    compressor::tans_encode(bytes, compact, compressor::TansTable::compact);
    EXPECT_LT(compact.size() + 200, full.size());
    Bytes out;
    ASSERT_TRUE(
        compressor::tans_decode(compact, bytes.size(), out, compressor::TansTable::compact));
    EXPECT_EQ(out, bytes);
    Bytes empty;
    compressor::tans_encode({}, empty, compressor::TansTable::compact);
    EXPECT_EQ(empty, Bytes{0});
    Bytes none;
    EXPECT_TRUE(compressor::tans_decode(empty, 0, none, compressor::TansTable::compact));
    // A gap past the last byte value, and a zero count.
    Bytes damaged = compact;
    damaged[1] = 0xFF;
    damaged.insert(damaged.begin() + 2, 0x7F);
    EXPECT_FALSE(
        compressor::tans_decode(damaged, bytes.size(), none, compressor::TansTable::compact));
    EXPECT_FALSE(compressor::tans_decode(Bytes{1, 0, 0}, 1, none, compressor::TansTable::compact));
}
