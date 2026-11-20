#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include <compressor/entropy/rans.h>
#include <compressor/error.h>

namespace
{

using Symbols = std::vector<uint16_t>;

// `count` symbols where 0 has probability `p0` and the rest spread over 1..alphabet-1.
Symbols skewed(std::size_t count, double p0, std::size_t alphabet, uint32_t seed = 1)
{
    Symbols symbols(count);
    for (uint16_t& symbol : symbols)
    {
        seed = (seed * 1103515245U) + 12345U;
        const double u = static_cast<double>(seed >> 8U) / static_cast<double>(1U << 24U);
        seed = (seed * 1103515245U) + 12345U;
        symbol = u < p0 ? 0 : static_cast<uint16_t>(1 + ((seed >> 16U) % (alphabet - 1)));
    }
    return symbols;
}

std::vector<uint8_t> encode(const Symbols& symbols, std::size_t alphabet)
{
    std::vector<uint8_t> out;
    compressor::rans_encode(symbols, alphabet, out);
    return out;
}

} // namespace

TEST(RansTests, Symbols_round_trip_for_every_shape_of_distribution)
{
    const std::vector<std::pair<Symbols, std::size_t>> cases{
        {Symbols{}, 4},
        {Symbols(1000, 3), 4},
        {skewed(10'000, 0.5, 2), 2},
        {skewed(50'000, 0.99, 257), 257},
        {skewed(50'000, 0.0, 257), 257},
        {skewed(3, 0.3, 300), 300},
    };
    for (const auto& [symbols, alphabet] : cases)
    {
        const auto decoded =
            compressor::rans_decode(encode(symbols, alphabet), alphabet, symbols.size());
        ASSERT_TRUE(decoded) << symbols.size();
        EXPECT_EQ(*decoded, symbols);
    }
}

TEST(RansTests, A_very_likely_symbol_costs_less_than_a_bit)
{
    // Huffman needs at least 1 bit per symbol, so 100 000 symbols take at least 12 500 bytes.
    const Symbols symbols = skewed(100'000, 0.98, 4);
    const std::vector<uint8_t> coded = encode(symbols, 4);
    EXPECT_LT(coded.size(), 3000U);
    // Order-0 entropy of the sample, to compare with: within 2% plus the table.
    std::vector<double> counts(4);
    for (const uint16_t symbol : symbols)
    {
        ++counts[symbol];
    }
    double bits = 0;
    for (const double count : counts)
    {
        if (count > 0)
        {
            bits -= count * std::log2(count / static_cast<double>(symbols.size()));
        }
    }
    EXPECT_LT(static_cast<double>(coded.size()), (bits / 8 * 1.02) + 16);
}

TEST(RansTests, Damaged_streams_are_rejected)
{
    const Symbols symbols = skewed(5000, 0.7, 10);
    const std::vector<uint8_t> coded = encode(symbols, 10);
    for (std::size_t length = 0; length < coded.size(); length += 7)
    {
        const auto result =
            compressor::rans_decode(std::span(coded).first(length), 10, symbols.size());
        EXPECT_FALSE(result) << length;
    }
    std::vector<uint8_t> longer = coded;
    longer.push_back(0);
    EXPECT_FALSE(compressor::rans_decode(longer, 10, symbols.size()));
    EXPECT_FALSE(compressor::rans_decode(coded, 10, symbols.size() + 1));
    // A frequency table that doesn't sum to 2^15.
    std::vector<uint8_t> bad_table = coded;
    bad_table[0] ^= 1U;
    EXPECT_FALSE(compressor::rans_decode(bad_table, 10, symbols.size()));
}
