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
#include <utility>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <compressor/data/data.h>
#include <compressor/data/reader.h>
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

compressor::EncodedData encoded_aab()
{
    // "aab": a and b both get length 1, so a = 0 and b = 1, and the payload is 001.
    compressor::EncodedData data;
    data.size_ = 3;
    data.code_lengths_['a'] = 1;
    data.code_lengths_['b'] = 1;
    data.payload_ = {0x20};
    return data;
}

compressor::EncodedData stored_abc()
{
    compressor::EncodedData data;
    data.method_ = compressor::Method::stored;
    data.size_ = 3;
    data.payload_ = {'a', 'b', 'c'};
    return data;
}

std::string header_bytes(uint64_t size, const compressor::CodeLengths& lengths)
{
    std::string bytes(137, '\0');
    bytes[0] = '\x01';
    for (std::size_t i = 0; i < 8; ++i)
    {
        bytes[1 + i] = static_cast<char>((size >> (8 * i)) & 0xFFU);
    }
    for (std::size_t i = 0; i < 128; ++i)
    {
        bytes[9 + i] = static_cast<char>(static_cast<unsigned>(lengths[2 * i]) |
                                         (static_cast<unsigned>(lengths[(2 * i) + 1]) << 4U));
    }
    return bytes;
}

} // namespace

TEST_F(CompressorDataTests, Encoded_data_is_written_in_the_portable_format)
{
    stream << encoded_aab();
    const std::string bytes = stream.str();

    ASSERT_EQ(bytes.size(), 138U);
    EXPECT_EQ(bytes[0], '\x01');
    EXPECT_EQ(bytes.substr(1, 8), std::string("\x03\0\0\0\0\0\0\0", 8));
    // 'a' (97) is odd: the high nibble of byte 48; 'b' (98) the low nibble of byte 49.
    EXPECT_EQ(bytes[9 + 48], '\x10');
    EXPECT_EQ(bytes[9 + 49], '\x01');
    EXPECT_EQ(std::ranges::count(bytes.substr(9, 128), '\0'), 126);
    // Payload 001, first bit in the most significant bit.
    EXPECT_EQ(bytes[137], '\x20');
}

TEST_F(CompressorDataTests, Stored_data_is_written_without_code_lengths)
{
    stream << stored_abc();

    EXPECT_EQ(stream.str(), std::string("\x00\x03\0\0\0\0\0\0\0abc", 12));
}

TEST_F(CompressorDataTests, Stored_data_round_trips_through_a_stream)
{
    stream << stored_abc();

    compressor::EncodedData output_data;
    stream >> output_data;

    ASSERT_FALSE(stream.fail());
    EXPECT_EQ(output_data, stored_abc());
}

TEST_F(CompressorDataTests, A_short_stored_header_fails)
{
    stream << stored_abc();
    std::stringstream truncated(stream.str().substr(0, 5));

    compressor::EncodedData output_data;
    truncated >> output_data;

    EXPECT_TRUE(truncated.fail());
    EXPECT_EQ(output_data, compressor::EncodedData{});
}

TEST_F(CompressorDataTests, EncodedDataStream)
{
    const compressor::EncodedData input_data = encoded_aab();
    stream << input_data;

    compressor::EncodedData output_data;
    stream >> output_data;

    ASSERT_FALSE(stream.fail());
    EXPECT_EQ(output_data, input_data);
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

    std::string unknown_method = header_bytes(3, valid);
    unknown_method[0] = '\x02';

    const std::array<std::pair<const char*, std::string>, 5> cases{{
        {"Kraft sum below 1", header_bytes(3, incomplete)},
        {"Kraft sum above 1", header_bytes(3, oversubscribed)},
        {"codes for empty data", header_bytes(0, valid)},
        {"no codes for non-empty data", header_bytes(3, compressor::CodeLengths{})},
        {"unknown method", unknown_method},
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

TEST_F(CompressorDataTests, Encoded_data_reads_from_a_stream_that_cannot_seek)
{
    compressor::EncodedData input_data = encoded_aab();
    input_data.payload_ = all_byte_values(100'000);
    stream << input_data;
    PipeBuffer pipe(stream.str(), 1000);
    std::istream input(&pipe);

    compressor::EncodedData output_data;
    input >> output_data;

    ASSERT_FALSE(input.fail());
    EXPECT_EQ(output_data, input_data);
}

namespace
{

class TempFile
{
public:
    explicit TempFile(const std::string& bytes)
        : path_(std::filesystem::temp_directory_path() /
                (std::string("compressor_data_") +
                 testing::UnitTest::GetInstance()->current_test_info()->name()))
    {
        std::ofstream stream(path_, std::ios::out | std::ios::trunc | std::ios::binary);
        stream << bytes;
    }

    ~TempFile()
    {
        std::error_code error;
        std::filesystem::remove(path_, error);
    }

    TempFile(const TempFile&) = delete;
    TempFile& operator=(const TempFile&) = delete;
    TempFile(TempFile&&) = delete;
    TempFile& operator=(TempFile&&) = delete;

    std::string path() const
    {
        return path_.string();
    }

private:
    std::filesystem::path path_;
};

template <class T> std::expected<T, compressor::Error> read_file(const TempFile& file)
{
    compressor::DataReader<T> reader;
    EXPECT_TRUE(reader.open(file.path()));
    auto result = reader.read();
    reader.close();
    return result;
}

} // namespace

TEST_F(CompressorDataTests, Reader_returns_valid_encoded_data)
{
    stream << encoded_aab();
    const TempFile file(stream.str());

    EXPECT_EQ(read_file<compressor::EncodedData>(file), encoded_aab());
}

TEST_F(CompressorDataTests, Reader_reports_a_short_header_as_corrupt)
{
    stream << encoded_aab();
    const TempFile file(stream.str().substr(0, 100));

    EXPECT_EQ(read_file<compressor::EncodedData>(file),
              std::unexpected(compressor::Error::corrupt_header));
}

TEST_F(CompressorDataTests, Reader_reports_an_invalid_header_as_corrupt)
{
    const TempFile file(header_bytes(3, compressor::CodeLengths{}) + '\x20');

    EXPECT_EQ(read_file<compressor::EncodedData>(file),
              std::unexpected(compressor::Error::corrupt_header));
}

TEST_F(CompressorDataTests, Reader_returns_an_empty_file_as_empty_decoded_data)
{
    const TempFile file("");

    EXPECT_EQ(read_file<compressor::DecodedData>(file), compressor::DecodedData{});
}
