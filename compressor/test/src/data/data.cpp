#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <sstream>
#include <string>
#include <utility>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <compressor/bitset/bitset.h>
#include <compressor/data/data.h>

class CompressorDataTests : public ::testing::Test
{
protected:
    std::stringstream stream;
    void SetUp() override
    {
        stream.clear();
    }
};

TEST_F(CompressorDataTests, DataStream)
{
    compressor::DecodedData input_data;
    input_data.data_ = {1, 2, 3, 4, 5};
    stream << input_data;

    compressor::DecodedData output_data;
    stream >> output_data;

    EXPECT_EQ(input_data.data_, output_data.data_);
    EXPECT_THAT(output_data.data_, testing::ElementsAre(1, 2, 3, 4, 5));
}

namespace
{

compressor::EncodedData encoded_aab()
{
    // "aab": a and b both get length 1, so a = 0 and b = 1, and the payload is 001.
    compressor::EncodedData data;
    data.size_ = 3;
    data.code_lengths_['a'] = 1;
    data.code_lengths_['b'] = 1;
    data.data_ = compressor::bitset(3);
    data.data_.set(2);
    return data;
}

std::string header_bytes(uint64_t size, const compressor::CodeLengths& lengths)
{
    std::string bytes(136, '\0');
    for (std::size_t i = 0; i < 8; ++i)
    {
        bytes[i] = static_cast<char>((size >> (8 * i)) & 0xFFU);
    }
    for (std::size_t i = 0; i < 128; ++i)
    {
        bytes[8 + i] = static_cast<char>(lengths[2 * i] | (lengths[(2 * i) + 1] << 4U));
    }
    return bytes;
}

} // namespace

TEST_F(CompressorDataTests, Encoded_data_is_written_in_the_portable_format)
{
    stream << encoded_aab();
    const std::string bytes = stream.str();

    ASSERT_EQ(bytes.size(), 137U);
    EXPECT_EQ(bytes.substr(0, 8), std::string("\x03\0\0\0\0\0\0\0", 8));
    // 'a' (97) is odd: the high nibble of byte 48; 'b' (98) the low nibble of byte 49.
    EXPECT_EQ(bytes[8 + 48], '\x10');
    EXPECT_EQ(bytes[8 + 49], '\x01');
    EXPECT_EQ(std::ranges::count(bytes.substr(8, 128), '\0'), 126);
    // Payload 001, first bit in the most significant bit.
    EXPECT_EQ(bytes[136], '\x20');
}

TEST_F(CompressorDataTests, EncodedDataStream)
{
    const compressor::EncodedData input_data = encoded_aab();
    stream << input_data;

    compressor::EncodedData output_data;
    stream >> output_data;

    ASSERT_FALSE(stream.fail());
    EXPECT_EQ(output_data.size_, input_data.size_);
    EXPECT_EQ(output_data.code_lengths_, input_data.code_lengths_);
    // The payload comes back padded to whole bytes.
    ASSERT_EQ(output_data.data_.size(), 8U);
    for (std::size_t bit = 0; bit < 8; ++bit)
    {
        EXPECT_EQ(output_data.data_.test(bit), bit == 2) << bit;
    }
}

TEST_F(CompressorDataTests, A_short_header_fails)
{
    stream << encoded_aab();
    std::stringstream truncated(stream.str().substr(0, 100));

    compressor::EncodedData output_data;
    truncated >> output_data;

    EXPECT_TRUE(truncated.fail());
    EXPECT_EQ(output_data, compressor::EncodedData{});
}

TEST_F(CompressorDataTests, Invalid_headers_fail)
{
    compressor::CodeLengths incomplete{};
    incomplete['a'] = 1;
    incomplete['b'] = 2;
    compressor::CodeLengths oversubscribed{};
    oversubscribed['a'] = 1;
    oversubscribed['b'] = 1;
    oversubscribed['c'] = 1;
    compressor::CodeLengths valid{};
    valid['a'] = 1;
    valid['b'] = 1;

    const std::array<std::pair<const char*, std::string>, 4> cases{{
        {"Kraft sum below 1", header_bytes(3, incomplete)},
        {"Kraft sum above 1", header_bytes(3, oversubscribed)},
        {"codes for empty data", header_bytes(0, valid)},
        {"no codes for non-empty data", header_bytes(3, compressor::CodeLengths{})},
    }};
    for (const auto& [name, bytes] : cases)
    {
        std::stringstream input(bytes + '\x20');
        compressor::EncodedData output_data;
        input >> output_data;
        EXPECT_TRUE(input.fail()) << name;
        EXPECT_EQ(output_data, compressor::EncodedData{}) << name;
    }
}
