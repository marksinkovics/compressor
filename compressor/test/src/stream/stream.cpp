#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <istream>
#include <memory>
#include <ostream>
#include <span>
#include <sstream>
#include <stdexcept>
#include <streambuf>
#include <string>
#include <utility>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <compressor/data/data.h>
#include <compressor/engine/engine.h>
#include <compressor/error.h>
#include <compressor/stream/decoder.h>
#include <compressor/stream/encoder.h>
#include <compressor/test/mock/engine/engine.h>

namespace
{

using ::testing::_;
using ::testing::Return;

constexpr uint8_t block_size_log2 = compressor::min_block_size_log2;
constexpr std::size_t block_size = std::size_t{1} << block_size_log2;

// Skewed letters, so Huffman wins, with a stored-looking stretch every few blocks.
std::string sample_text(std::size_t size)
{
    std::string text(size, '\0');
    uint32_t state = 7;
    for (std::size_t i = 0; i < size; ++i)
    {
        state = (state * 1103515245U) + 12345U;
        const bool noisy = (i / block_size) % 3 == 2;
        text[i] = noisy
                      ? static_cast<char>(state >> 16U)
                      : static_cast<char>('a' + ((state >> 16U) % 5 == 0 ? (state >> 8U) % 7 : 0));
    }
    return text;
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

// istream turns the exception into badbit.
class BrokenInput : public std::streambuf
{
protected:
    int_type underflow() override
    {
        throw std::runtime_error("disk error");
    }
};

class FullOutput : public std::streambuf
{
protected:
    int_type overflow(int_type /*c*/) override
    {
        return traits_type::eof();
    }
};

std::expected<std::string, compressor::Error> encode(std::istream& in)
{
    compressor::BlockEncoder encoder(std::make_unique<compressor::Engine>(), block_size_log2);
    std::ostringstream out;
    auto result = encoder.encode(in, out);
    if (!result)
    {
        return std::unexpected(result.error());
    }
    return out.str();
}

std::expected<std::string, compressor::Error> encode(const std::string& text)
{
    std::istringstream in(text);
    return encode(in);
}

std::expected<std::string, compressor::Error> decode(const std::string& bytes)
{
    compressor::BlockDecoder decoder;
    std::istringstream in(bytes);
    std::ostringstream out;
    auto result = decoder.decode(in, out);
    if (!result)
    {
        return std::unexpected(result.error());
    }
    return out.str();
}

std::string in_memory_encoding(const std::string& text)
{
    compressor::DecodedData data;
    data.data_.assign(text.begin(), text.end());
    compressor::Engine engine(block_size_log2);
    std::ostringstream out;
    out << engine.encode(data);
    return out.str();
}

std::size_t block_count(const std::string& bytes)
{
    std::istringstream in(bytes);
    auto data = compressor::read_encoded(in);
    return data ? data->blocks_.size() : 0;
}

} // namespace

class BlockStreamSizeTests : public testing::TestWithParam<std::size_t>
{
};

TEST_P(BlockStreamSizeTests, Round_trips_and_matches_the_in_memory_encoding)
{
    const std::string text = sample_text(GetParam());
    const auto encoded = encode(text);
    ASSERT_TRUE(encoded);

    EXPECT_EQ(*encoded, in_memory_encoding(text));
    EXPECT_EQ(block_count(*encoded), (text.size() + block_size - 1) / block_size);
    EXPECT_EQ(decode(*encoded), text);
}

INSTANTIATE_TEST_SUITE_P(Sizes, BlockStreamSizeTests,
                         testing::Values(0, 1, block_size - 1, block_size, block_size + 1,
                                         3 * block_size),
                         [](const testing::TestParamInfo<std::size_t>& info) {
                             return "bytes_" + std::to_string(info.param);
                         });

TEST(BlockStreamTests, The_encoder_reads_from_a_stream_that_cannot_seek)
{
    const std::string text = sample_text((3 * block_size) + 17);
    PipeBuffer pipe(text, 1000);
    std::istream in(&pipe);

    EXPECT_EQ(encode(in), in_memory_encoding(text));
}

TEST(BlockStreamTests, The_decoder_reads_from_a_stream_that_cannot_seek)
{
    const std::string text = sample_text((3 * block_size) + 17);
    PipeBuffer pipe(*encode(text), 1000);
    std::istream in(&pipe);
    std::ostringstream out;

    compressor::BlockDecoder decoder;
    ASSERT_TRUE(decoder.decode(in, out));
    EXPECT_EQ(out.str(), text);
}

TEST(BlockStreamTests, The_encoder_passes_one_block_at_a_time_to_the_engine)
{
    auto engine = std::make_unique<MockIEngine>();
    std::vector<std::size_t> sizes;
    EXPECT_CALL(*engine, encode_block(_)).Times(3).WillRepeatedly([&](std::span<const uint8_t> b) {
        sizes.push_back(b.size());
        return compressor::EncodedBlock{};
    });
    compressor::BlockEncoder encoder(std::move(engine), block_size_log2);
    std::istringstream in(std::string((2 * block_size) + 1, 'a'));
    std::ostringstream out;

    ASSERT_TRUE(encoder.encode(in, out));
    EXPECT_EQ(sizes, (std::vector<std::size_t>{block_size, block_size, 1}));
}

TEST(BlockStreamTests, The_decoder_stops_at_the_first_block_the_engine_rejects)
{
    auto engine = std::make_unique<MockIEngine>();
    EXPECT_CALL(*engine, decode_block(_, _))
        .WillOnce(Return(std::unexpected(compressor::Error::checksum_mismatch)));
    compressor::BlockDecoder decoder(std::move(engine));
    std::istringstream in(*encode(sample_text(3 * block_size)));
    std::ostringstream out;

    EXPECT_EQ(decoder.decode(in, out), std::unexpected(compressor::Error::checksum_mismatch));
    EXPECT_EQ(out.str(), "");
}

TEST(BlockStreamTests, A_changed_byte_in_a_later_block_fails_after_the_earlier_blocks)
{
    const std::string text = sample_text(3 * block_size);
    std::string bytes = *encode(text);
    bytes[bytes.size() - 20] = bytes[bytes.size() - 20] == 'A' ? 'B' : 'A';
    compressor::BlockDecoder decoder;
    std::istringstream in(bytes);
    std::ostringstream out;

    EXPECT_FALSE(decoder.decode(in, out));
    EXPECT_EQ(out.str(), text.substr(0, 2 * block_size));
}

TEST(BlockStreamTests, The_decoder_reports_container_errors)
{
    const std::string bytes = *encode(sample_text(block_size + 1));

    EXPECT_EQ(decode("hello"), std::unexpected(compressor::Error::not_ingot));
    EXPECT_EQ(decode(bytes.substr(0, bytes.size() - 1)),
              std::unexpected(compressor::Error::truncated));
    EXPECT_EQ(decode(bytes + "x"), std::unexpected(compressor::Error::corrupt_header));
}

TEST(BlockStreamTests, A_failing_input_is_a_read_error)
{
    BrokenInput broken;
    std::istream in(&broken);

    EXPECT_EQ(encode(in), std::unexpected(compressor::Error::read_failed));

    std::istream encoded_in(&broken);
    std::ostringstream out;
    compressor::BlockDecoder decoder;
    EXPECT_EQ(decoder.decode(encoded_in, out), std::unexpected(compressor::Error::read_failed));
}

TEST(BlockStreamTests, A_failing_output_is_a_write_error)
{
    FullOutput full;
    std::ostream out(&full);

    compressor::BlockEncoder encoder;
    std::istringstream text_in(sample_text(10));
    EXPECT_EQ(encoder.encode(text_in, out), std::unexpected(compressor::Error::write_failed));

    out.clear();
    compressor::BlockDecoder decoder;
    std::istringstream encoded_in(*encode(sample_text(10)));
    EXPECT_EQ(decoder.decode(encoded_in, out), std::unexpected(compressor::Error::write_failed));
}
