#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>

#include <gtest/gtest.h>

#include <compressor/huffman/huffman.h>

namespace
{

using compressor::CodeLengths;
using compressor::Frequencies;

Frequencies frequencies_of(const std::string& text)
{
    Frequencies frequencies{};
    for (const char c : text)
    {
        ++frequencies[static_cast<uint8_t>(c)];
    }
    return frequencies;
}

uint64_t total_bits(const Frequencies& frequencies, const CodeLengths& lengths)
{
    uint64_t bits = 0;
    for (std::size_t i = 0; i < compressor::symbol_count; ++i)
    {
        bits += frequencies[i] * lengths[i];
    }
    return bits;
}

// 1, 1, 2, 3, 5, ...: plain Huffman gives the deepest symbol a code of length count - 1.
Frequencies fibonacci_frequencies(std::size_t count)
{
    Frequencies frequencies{};
    uint64_t a = 1;
    uint64_t b = 1;
    for (std::size_t i = 0; i < count; ++i)
    {
        frequencies[i] = a;
        const uint64_t next = a + b;
        a = b;
        b = next;
    }
    return frequencies;
}

uint8_t max_length(const CodeLengths& lengths)
{
    return *std::ranges::max_element(lengths);
}

} // namespace

TEST(HuffmanTests, No_symbols_give_no_codes)
{
    const CodeLengths lengths = compressor::code_lengths(Frequencies{});
    EXPECT_EQ(lengths, CodeLengths{});
    EXPECT_TRUE(compressor::valid_code_lengths(lengths));
}

TEST(HuffmanTests, A_single_symbol_gets_the_code_0)
{
    const CodeLengths lengths = compressor::code_lengths(frequencies_of("aaaa"));
    EXPECT_EQ(lengths['a'], 1);
    EXPECT_EQ(std::ranges::count(lengths, 0), 255);
    EXPECT_TRUE(compressor::valid_code_lengths(lengths));
    EXPECT_EQ(compressor::canonical_codes(lengths)['a'],
              (compressor::Code{.bits = 0, .length = 1}));
}

TEST(HuffmanTests, Lengths_match_plain_huffman_when_the_cap_is_not_reached)
{
    const Frequencies frequencies = frequencies_of("go go gophers");
    const CodeLengths lengths = compressor::code_lengths(frequencies);
    EXPECT_EQ(total_bits(frequencies, lengths), 37U);
    EXPECT_TRUE(compressor::valid_code_lengths(lengths));
}

TEST(HuffmanTests, Package_merge_follows_the_cap)
{
    Frequencies frequencies{};
    frequencies['a'] = 1;
    frequencies['b'] = 1;
    frequencies['c'] = 2;
    frequencies['d'] = 4;

    const CodeLengths uncapped = compressor::code_lengths(frequencies, 3);
    EXPECT_EQ(uncapped['a'], 3);
    EXPECT_EQ(uncapped['b'], 3);
    EXPECT_EQ(uncapped['c'], 2);
    EXPECT_EQ(uncapped['d'], 1);

    const CodeLengths capped = compressor::code_lengths(frequencies, 2);
    for (const char symbol : std::string("abcd"))
    {
        EXPECT_EQ(capped[static_cast<uint8_t>(symbol)], 2) << symbol;
    }
}

TEST(HuffmanTests, Skewed_frequencies_are_capped_at_the_maximum_length)
{
    const Frequencies frequencies = fibonacci_frequencies(20);

    const CodeLengths uncapped = compressor::code_lengths(frequencies, 19);
    EXPECT_EQ(max_length(uncapped), 19);

    const CodeLengths capped = compressor::code_lengths(frequencies);
    EXPECT_EQ(max_length(capped), compressor::max_code_length);
    EXPECT_TRUE(compressor::valid_code_lengths(capped));
    EXPECT_GE(total_bits(frequencies, capped), total_bits(frequencies, uncapped));
}

TEST(HuffmanTests, Equal_frequencies_over_all_bytes_give_8_bit_codes)
{
    Frequencies frequencies{};
    frequencies.fill(7);
    const CodeLengths lengths = compressor::code_lengths(frequencies);
    EXPECT_EQ(std::ranges::count(lengths, 8), 256);
}

TEST(HuffmanTests, Canonical_codes_follow_rfc_1951)
{
    CodeLengths lengths{};
    const std::string symbols = "ABCDEFGH";
    const std::array<uint8_t, 8> example{3, 3, 3, 3, 3, 2, 4, 4};
    for (std::size_t i = 0; i < symbols.size(); ++i)
    {
        lengths[static_cast<uint8_t>(symbols[i])] = example[i];
    }

    const compressor::Codes codes = compressor::canonical_codes(lengths);
    const std::array<uint32_t, 8> expected{0b010, 0b011, 0b100, 0b101, 0b110, 0b00, 0b1110, 0b1111};
    for (std::size_t i = 0; i < symbols.size(); ++i)
    {
        const auto symbol = static_cast<uint8_t>(symbols[i]);
        EXPECT_EQ(codes[symbol], (compressor::Code{.bits = expected[i], .length = example[i]}))
            << symbols[i];
    }
}

TEST(HuffmanTests, Invalid_code_lengths_are_rejected)
{
    CodeLengths too_long{};
    too_long['a'] = 1;
    too_long['b'] = 2;
    too_long['c'] = compressor::max_code_length + 1;
    too_long['d'] = compressor::max_code_length + 1;
    EXPECT_FALSE(compressor::valid_code_lengths(too_long));

    CodeLengths oversubscribed{};
    oversubscribed['a'] = 1;
    oversubscribed['b'] = 1;
    oversubscribed['c'] = 1;
    EXPECT_FALSE(compressor::valid_code_lengths(oversubscribed));

    CodeLengths incomplete{};
    incomplete['a'] = 2;
    incomplete['b'] = 2;
    EXPECT_FALSE(compressor::valid_code_lengths(incomplete));

    CodeLengths single_too_long{};
    single_too_long['a'] = 2;
    EXPECT_FALSE(compressor::valid_code_lengths(single_too_long));
}

TEST(HuffmanTests, Decode_table_maps_every_bit_pattern_to_the_code_it_starts_with)
{
    CodeLengths lengths{};
    const std::string symbols = "ABCDEFGH";
    const std::array<uint8_t, 8> example{3, 3, 3, 3, 3, 2, 4, 4};
    for (std::size_t i = 0; i < symbols.size(); ++i)
    {
        lengths[static_cast<uint8_t>(symbols[i])] = example[i];
    }
    const compressor::Codes codes = compressor::canonical_codes(lengths);
    const compressor::DecodeTable table = compressor::build_decode_table(codes);

    ASSERT_EQ(table.size(), std::size_t{1} << compressor::max_code_length);
    for (std::size_t pattern = 0; pattern < table.size(); ++pattern)
    {
        const char expected = *std::ranges::find_if(symbols, [&](char symbol) {
            const compressor::Code& code = codes[static_cast<uint8_t>(symbol)];
            return (pattern >> (compressor::max_code_length - code.length)) == code.bits;
        });
        ASSERT_EQ(table[pattern].symbol, static_cast<uint8_t>(expected)) << pattern;
        ASSERT_EQ(table[pattern].length, lengths[static_cast<uint8_t>(expected)]) << pattern;
    }
}

TEST(HuffmanTests, Decode_table_marks_patterns_that_start_no_code)
{
    const compressor::DecodeTable table = compressor::build_decode_table(
        compressor::canonical_codes(compressor::code_lengths(frequencies_of("aaaa"))));

    const std::size_t half = table.size() / 2;
    for (std::size_t pattern = 0; pattern < table.size(); ++pattern)
    {
        ASSERT_EQ(table[pattern].length, pattern < half ? 1 : 0) << pattern;
        if (pattern < half)
        {
            ASSERT_EQ(table[pattern].symbol, 'a') << pattern;
        }
    }
}
