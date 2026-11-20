#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <streambuf>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <compressor/checksum/crc32.h>
#include <compressor/data/data.h>
#include <compressor/error.h>

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

// Every byte value, then a pseudo-random tail, `size` bytes in all.
std::vector<uint8_t> all_byte_values(std::size_t size)
{
    std::vector<uint8_t> bytes(size);
    uint32_t state = 12345;
    for (std::size_t i = 0; i < size; ++i)
    {
        state = (state * 1103515245U) + 12345U;
        bytes[i] = i < 256 ? static_cast<uint8_t>(i) : static_cast<uint8_t>(state >> 16U);
    }
    return bytes;
}

// Hands out `chunk` bytes per underflow and can't seek, like a pipe.
class PipeBuffer : public std::streambuf
{
public:
    PipeBuffer(std::string bytes, std::size_t chunk)
        : bytes_(std::move(bytes))
        , chunk_(chunk)
    {
    }

protected:
    int_type underflow() override
    {
        if (next_ >= bytes_.size())
        {
            return traits_type::eof();
        }
        const std::size_t count = std::min(chunk_, bytes_.size() - next_);
        char* begin = bytes_.data() + next_;
        setg(begin, begin, begin + count);
        next_ += count;
        return traits_type::to_int_type(*begin);
    }

private:
    std::string bytes_;
    std::size_t chunk_;
    std::size_t next_ = 0;
};

} // namespace

TEST_F(CompressorDataTests, Decoded_data_round_trips_every_byte_value)
{
    compressor::DecodedData input_data;
    input_data.data_ = all_byte_values(256);
    stream << input_data;

    compressor::DecodedData output_data;
    output_data.data_ = {9, 9, 9};
    stream >> output_data;

    EXPECT_EQ(output_data, input_data);
}

TEST_F(CompressorDataTests, Decoded_data_reads_across_blocks)
{
    compressor::DecodedData input_data;
    input_data.data_ = all_byte_values((std::size_t{3} << 20U) + 7);
    stream << input_data;

    compressor::DecodedData output_data;
    stream >> output_data;

    EXPECT_EQ(output_data, input_data);
}

TEST_F(CompressorDataTests, Decoded_data_reads_from_a_stream_that_cannot_seek)
{
    const std::vector<uint8_t> bytes = all_byte_values(200'000);
    PipeBuffer pipe(std::string(bytes.begin(), bytes.end()), 1000);
    std::istream input(&pipe);

    compressor::DecodedData output_data;
    input >> output_data;

    EXPECT_EQ(output_data.data_, bytes);
}

namespace
{

uint32_t crc(std::string_view text)
{
    return compressor::crc32({reinterpret_cast<const uint8_t*>(text.data()), text.size()});
}

compressor::EncodedBlock huffman_aab()
{
    // "aab": a and b both get length 1, so a = 0 and b = 1, and the payload is 001.
    compressor::EncodedBlock block;
    block.size_ = 3;
    block.crc_ = crc("aab");
    block.code_lengths_['a'] = 1;
    block.code_lengths_['b'] = 1;
    block.payload_ = {0x20};
    return block;
}

compressor::EncodedBlock stored_block(std::string_view text)
{
    compressor::EncodedBlock block;
    block.method_ = compressor::Method::stored;
    block.size_ = static_cast<uint32_t>(text.size());
    block.crc_ = crc(text);
    block.payload_.assign(text.begin(), text.end());
    return block;
}

compressor::EncodedData container(std::vector<compressor::EncodedBlock> blocks,
                                  uint8_t block_size_log2 = compressor::default_block_size_log2)
{
    compressor::EncodedData data;
    data.block_size_log2_ = block_size_log2;
    data.blocks_ = std::move(blocks);
    return data;
}

compressor::EncodedData encoded_aab()
{
    return container({huffman_aab()});
}

compressor::EncodedData stored_abc()
{
    return container({stored_block("abc")});
}

std::string serialized(const compressor::EncodedData& data)
{
    std::stringstream stream;
    stream << data;
    return stream.str();
}

std::string le32(uint32_t value)
{
    std::string bytes;
    for (std::size_t i = 0; i < 4; ++i)
    {
        bytes.push_back(static_cast<char>((value >> (8 * i)) & 0xFFU));
    }
    return bytes;
}

void put_le32(std::string& bytes, std::size_t offset, uint32_t value)
{
    bytes.replace(offset, 4, le32(value));
}

std::expected<compressor::EncodedData, compressor::Error> parse(const std::string& bytes)
{
    std::stringstream input(bytes);
    return compressor::read_encoded(input);
}

// Offsets in a container whose first block is stored_abc's or huffman_aab's.
constexpr std::size_t method_offset = 8;
constexpr std::size_t size_offset = 9;
constexpr std::size_t encoded_size_offset = 13;
constexpr std::size_t lengths_offset = 21;

} // namespace

TEST_F(CompressorDataTests, Stored_data_is_written_in_the_container_format)
{
    const std::string bytes = serialized(stored_abc());

    ASSERT_EQ(bytes.size(), 8U + 13U + 3U + 9U);
    EXPECT_EQ(bytes.substr(0, 8), std::string("\x89ING\x01\x14\0\0", 8));
    EXPECT_EQ(bytes.substr(8, 13), std::string("\x00", 1) + le32(3) + le32(3) + le32(crc("abc")));
    EXPECT_EQ(bytes.substr(21, 3), "abc");
    EXPECT_EQ(bytes.substr(24), std::string("\xFF\x03\0\0\0\0\0\0\0", 9));
}

TEST_F(CompressorDataTests, Huffman_blocks_are_written_with_nibble_code_lengths)
{
    const std::string bytes = serialized(encoded_aab());

    ASSERT_EQ(bytes.size(), 8U + 13U + 128U + 1U + 9U);
    EXPECT_EQ(bytes.substr(8, 13), std::string("\x01", 1) + le32(3) + le32(129) + le32(crc("aab")));
    // 'a' (97) is odd: the high nibble of byte 48; 'b' (98) the low nibble of byte 49.
    EXPECT_EQ(bytes[lengths_offset + 48], '\x10');
    EXPECT_EQ(bytes[lengths_offset + 49], '\x01');
    EXPECT_EQ(std::ranges::count(bytes.substr(lengths_offset, 128), '\0'), 126);
    // Payload 001, first bit in the most significant bit.
    EXPECT_EQ(bytes[lengths_offset + 128], '\x20');
    EXPECT_EQ(bytes.substr(150), std::string("\xFF\x03\0\0\0\0\0\0\0", 9));
}

TEST_F(CompressorDataTests, Empty_data_is_a_frame_header_and_an_end_block)
{
    const std::string bytes = serialized(container({}));

    EXPECT_EQ(bytes, std::string("\x89ING\x01\x14\0\0\xFF\0\0\0\0\0\0\0\0", 17));
    EXPECT_EQ(parse(bytes), container({}));
}

TEST_F(CompressorDataTests, Encoded_data_round_trips_through_a_stream)
{
    const std::string full(4096, 'x');
    const std::array cases{
        stored_abc(),
        encoded_aab(),
        container({stored_block(full), huffman_aab()}, 12),
    };
    for (const auto& input_data : cases)
    {
        std::stringstream stream;
        stream << input_data;

        compressor::EncodedData output_data;
        stream >> output_data;

        ASSERT_FALSE(stream.fail());
        EXPECT_EQ(output_data, input_data);
    }
}

TEST_F(CompressorDataTests, Every_truncation_is_reported)
{
    const std::string bytes =
        serialized(container({stored_block(std::string(4096, 'x')), huffman_aab()}, 12));
    for (std::size_t length = 0; length < bytes.size(); ++length)
    {
        const auto expected =
            length < 4 ? compressor::Error::not_ingot : compressor::Error::truncated;
        EXPECT_EQ(parse(bytes.substr(0, length)), std::unexpected(expected)) << length;
    }
}

TEST_F(CompressorDataTests, Invalid_frame_headers_are_rejected)
{
    const std::string valid = serialized(stored_abc());
    auto with = [&](std::size_t offset, char value) {
        std::string bytes = valid;
        bytes[offset] = value;
        return bytes;
    };

    using compressor::Error;
    EXPECT_EQ(parse("plain text, not ingot"), std::unexpected(Error::not_ingot));
    EXPECT_EQ(parse(with(0, 'I')), std::unexpected(Error::not_ingot));
    EXPECT_EQ(parse(with(3, 'Z')), std::unexpected(Error::not_ingot));
    EXPECT_EQ(parse(with(4, '\x02')), std::unexpected(Error::unsupported_version));
    EXPECT_EQ(parse(with(4, '\x00')), std::unexpected(Error::unsupported_version));
    EXPECT_EQ(parse(with(5, '\x0B')), std::unexpected(Error::corrupt_header));
    EXPECT_EQ(parse(with(5, '\x1B')), std::unexpected(Error::corrupt_header));
    EXPECT_EQ(parse(with(6, '\x02')), std::unexpected(Error::corrupt_header));
    EXPECT_EQ(parse(with(7, '\x80')), std::unexpected(Error::corrupt_header));
}

TEST_F(CompressorDataTests, Invalid_block_headers_are_rejected)
{
    using compressor::Error;
    const std::string stored = serialized(stored_abc());
    const std::string huffman = serialized(encoded_aab());

    std::string unknown_method = stored;
    unknown_method[method_offset] = '\x02';
    EXPECT_EQ(parse(unknown_method), std::unexpected(Error::corrupt_header));

    std::string empty_block = stored;
    put_le32(empty_block, size_offset, 0);
    EXPECT_EQ(parse(empty_block), std::unexpected(Error::corrupt_header));

    std::string stored_size_mismatch = stored;
    put_le32(stored_size_mismatch, encoded_size_offset, 2);
    EXPECT_EQ(parse(stored_size_mismatch), std::unexpected(Error::corrupt_header));

    std::string no_room_for_lengths = huffman;
    put_le32(no_room_for_lengths, encoded_size_offset, 127);
    EXPECT_EQ(parse(no_room_for_lengths), std::unexpected(Error::corrupt_header));

    // Three symbols of at most 15 bits fit in 6 bytes.
    std::string payload_too_long = huffman;
    put_le32(payload_too_long, encoded_size_offset, 128 + 7);
    EXPECT_EQ(parse(payload_too_long), std::unexpected(Error::corrupt_header));
}

TEST_F(CompressorDataTests, A_block_larger_than_the_block_size_is_rejected)
{
    const std::string bytes = serialized(container({stored_block(std::string(4097, 'x'))}, 12));

    EXPECT_EQ(parse(bytes), std::unexpected(compressor::Error::corrupt_header));
}

TEST_F(CompressorDataTests, Only_the_last_block_may_be_short)
{
    const std::string bytes = serialized(container({stored_block("abc"), stored_block("def")}, 12));

    EXPECT_EQ(parse(bytes), std::unexpected(compressor::Error::corrupt_header));
}

TEST_F(CompressorDataTests, Invalid_code_lengths_are_rejected)
{
    compressor::CodeLengths incomplete{};
    incomplete['a'] = 1;
    incomplete['b'] = 2;
    compressor::CodeLengths oversubscribed{};
    oversubscribed['a'] = 1;
    oversubscribed['b'] = 1;
    oversubscribed['c'] = 1;

    const std::array<std::pair<const char*, compressor::CodeLengths>, 3> cases{{
        {"Kraft sum below 1", incomplete},
        {"Kraft sum above 1", oversubscribed},
        {"no codes", compressor::CodeLengths{}},
    }};
    for (const auto& [name, lengths] : cases)
    {
        compressor::EncodedBlock block = huffman_aab();
        block.code_lengths_ = lengths;
        EXPECT_EQ(parse(serialized(container({block}))),
                  std::unexpected(compressor::Error::corrupt_header))
            << name;
    }
}

TEST_F(CompressorDataTests, A_wrong_total_size_in_the_end_block_is_rejected)
{
    std::string bytes = serialized(stored_abc());
    bytes[25] = '\x04';

    EXPECT_EQ(parse(bytes), std::unexpected(compressor::Error::corrupt_header));
}

TEST_F(CompressorDataTests, Bytes_after_the_end_block_are_rejected)
{
    EXPECT_EQ(parse(serialized(stored_abc()) + 'x'),
              std::unexpected(compressor::Error::corrupt_header));
}

TEST_F(CompressorDataTests, A_failed_read_sets_failbit_and_leaves_the_data_empty)
{
    std::stringstream input(serialized(stored_abc()).substr(0, 20));
    compressor::EncodedData output_data = encoded_aab();
    input >> output_data;

    EXPECT_TRUE(input.fail());
    EXPECT_EQ(output_data, compressor::EncodedData{});
}

TEST_F(CompressorDataTests, Encoded_data_reads_from_a_stream_that_cannot_seek)
{
    const std::vector<uint8_t> big = all_byte_values(100'000);
    const compressor::EncodedData input_data = container(
        {stored_block(std::string_view(reinterpret_cast<const char*>(big.data()), big.size()))});
    PipeBuffer pipe(serialized(input_data), 1000);
    std::istream input(&pipe);

    compressor::EncodedData output_data;
    input >> output_data;

    ASSERT_FALSE(input.fail());
    EXPECT_EQ(output_data, input_data);
}

TEST_F(CompressorDataTests, Frame_reader_returns_blocks_one_at_a_time_without_checking_crcs)
{
    std::string bytes = serialized(container(
        {stored_block(std::string(4096, 'x')), stored_block(std::string(4096, 'y'))}, 12));
    // The second block's first payload byte, which only the engine's CRC check catches.
    bytes[8 + 13 + 4096 + 13] = 'z';
    stream.str(bytes);

    compressor::FrameReader reader(stream);
    ASSERT_EQ(reader.read_header(), 12);
    compressor::EncodedBlock block;
    ASSERT_EQ(reader.read_block(block), true);
    EXPECT_EQ(block.payload_, std::vector<uint8_t>(4096, 'x'));
    ASSERT_EQ(reader.read_block(block), true);
    EXPECT_EQ(block.payload_[0], 'z');
    EXPECT_EQ(reader.read_block(block), false);
}

TEST_F(CompressorDataTests, Frame_reader_rejects_a_block_after_a_short_one)
{
    const std::string bytes = serialized(
        container({stored_block(std::string(100, 'x')), stored_block(std::string(100, 'y'))}, 12));
    stream.str(bytes);

    compressor::FrameReader reader(stream);
    ASSERT_TRUE(reader.read_header());
    compressor::EncodedBlock block;
    ASSERT_EQ(reader.read_block(block), true);
    EXPECT_EQ(reader.read_block(block), std::unexpected(compressor::Error::corrupt_header));
}

TEST_F(CompressorDataTests, The_archive_flag_is_read_and_the_end_block_may_be_followed)
{
    std::stringstream bytes;
    compressor::write_frame_header(bytes, 12, compressor::archive_flag);
    compressor::write_block(bytes, stored_block("abc"));
    compressor::write_end_block(bytes, 3);
    bytes << "index";
    stream.str(bytes.str());

    compressor::FrameReader reader(stream);
    ASSERT_EQ(reader.read_header(), 12);
    EXPECT_EQ(reader.flags(), compressor::archive_flag);
    compressor::EncodedBlock block;
    ASSERT_EQ(reader.read_block(block), true);
    ASSERT_EQ(reader.read_block(block), false);
    EXPECT_EQ(reader.total_size(), 3U);
    EXPECT_EQ(stream.tellg(), 8 + 13 + 3 + 9);
}

TEST_F(CompressorDataTests, An_archive_is_not_read_as_encoded_data)
{
    std::string bytes = serialized(stored_abc());
    bytes[6] = '\x01';

    EXPECT_EQ(parse(bytes), std::unexpected(compressor::Error::unexpected_archive));
}

TEST_F(CompressorDataTests, Written_size_counts_the_header_and_the_code_lengths)
{
    EXPECT_EQ(compressor::written_size(stored_block("abc")), 13U + 3U);
    EXPECT_EQ(compressor::written_size(huffman_aab()), 13U + 128U + 1U);
}
