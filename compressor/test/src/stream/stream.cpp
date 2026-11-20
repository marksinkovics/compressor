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
#include <string_view>
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

compressor::EngineFactory engine_factory(int* made = nullptr)
{
    return [made] {
        if (made != nullptr)
        {
            ++*made;
        }
        return std::make_unique<compressor::Engine>(block_size_log2);
    };
}

std::expected<std::string, compressor::Error> encode_with(unsigned threads, const std::string& text)
{
    compressor::BlockEncoder encoder(engine_factory(), block_size_log2, threads);
    std::istringstream in(text);
    std::ostringstream out;
    if (auto result = encoder.encode(in, out); !result)
    {
        return std::unexpected(result.error());
    }
    return out.str();
}

// The decoded bytes, also when decoding fails.
std::pair<std::expected<void, compressor::Error>, std::string> decode_with(unsigned threads,
                                                                           const std::string& bytes)
{
    compressor::BlockDecoder decoder(engine_factory(), threads);
    std::istringstream in(bytes);
    std::ostringstream out;
    auto result = decoder.decode(in, out);
    return {result, out.str()};
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
    // The next block may already be in flight when the first one fails.
    EXPECT_CALL(*engine, decode_block(_, _))
        .Times(testing::Between(1, 2))
        .WillRepeatedly(Return(std::unexpected(compressor::Error::checksum_mismatch)));
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

class BlockStreamThreadTests : public testing::TestWithParam<unsigned>
{
};

TEST_P(BlockStreamThreadTests, The_output_does_not_depend_on_the_thread_count)
{
    const std::string text = sample_text((37 * block_size) + 5);
    const auto encoded = encode_with(GetParam(), text);
    ASSERT_TRUE(encoded);
    EXPECT_EQ(*encoded, in_memory_encoding(text));

    const auto [result, decoded] = decode_with(GetParam(), *encoded);
    EXPECT_TRUE(result);
    EXPECT_EQ(decoded, text);
}

TEST_P(BlockStreamThreadTests, A_bad_block_fails_after_writing_every_block_before_it)
{
    const std::string text = sample_text(20 * block_size);
    const std::string encoded = *encode_with(1, text);
    // Flip the last payload byte of block 11, just before block 12's header.
    std::istringstream in(encoded);
    const auto data = compressor::read_encoded(in);
    ASSERT_TRUE(data);
    std::size_t offset = compressor::frame_header_bytes;
    for (std::size_t i = 0; i <= 11; ++i)
    {
        const compressor::EncodedBlock& block = data->blocks_[i];
        offset +=
            compressor::block_header_bytes + block.payload_.size() +
            (block.method_ == compressor::Method::huffman ? compressor::code_lengths_bytes : 0);
    }
    std::string corrupt = encoded;
    corrupt[offset - 1] = static_cast<char>(static_cast<uint8_t>(corrupt[offset - 1]) ^ 0x55U);

    const auto [result, decoded] = decode_with(GetParam(), corrupt);
    EXPECT_FALSE(result);
    EXPECT_EQ(decoded, text.substr(0, 11 * block_size));
}

TEST_P(BlockStreamThreadTests, A_truncated_input_fails_after_writing_the_complete_blocks)
{
    const std::string text = sample_text(9 * block_size);
    const std::string encoded = *encode_with(1, text);

    const auto [result, decoded] = decode_with(GetParam(), encoded.substr(0, encoded.size() / 2));
    EXPECT_EQ(result, std::unexpected(compressor::Error::truncated));
    EXPECT_FALSE(decoded.empty());
    EXPECT_EQ(decoded.size() % block_size, 0U);
    EXPECT_EQ(decoded, text.substr(0, decoded.size()));
}

INSTANTIATE_TEST_SUITE_P(Threads, BlockStreamThreadTests, testing::Values(1U, 2U, 3U, 8U),
                         [](const testing::TestParamInfo<unsigned>& info) {
                             return "threads_" + std::to_string(info.param);
                         });

TEST(BlockStreamTests, The_coders_make_one_engine_per_thread)
{
    int made = 0;
    const compressor::BlockEncoder encoder(engine_factory(&made), block_size_log2, 3);
    EXPECT_EQ(made, 3);
    const compressor::BlockDecoder decoder(engine_factory(&made), 0);
    EXPECT_EQ(made, 4);
}

TEST(BlockStreamTests, Encode_frame_reports_where_each_block_starts)
{
    compressor::BlockEncoder encoder(engine_factory(), block_size_log2, 2);
    const std::string text = sample_text((2 * block_size) + 5);
    std::istringstream in(text);
    std::ostringstream out;

    const auto layout = encoder.encode_frame(in, out, compressor::archive_flag);
    ASSERT_TRUE(layout);
    const std::string bytes = out.str();
    EXPECT_EQ(layout->size, bytes.size());
    EXPECT_EQ(layout->total_size, text.size());
    ASSERT_EQ(layout->block_offsets.size(), 3U);
    EXPECT_EQ(layout->block_offsets[0], compressor::frame_header_bytes);
    // Each offset is a block header: method 0 or 1, then the full block size.
    for (const uint64_t offset : layout->block_offsets)
    {
        EXPECT_LE(static_cast<uint8_t>(bytes[offset]), 1);
    }
    EXPECT_EQ(static_cast<uint8_t>(bytes[layout->size - compressor::end_block_bytes]), 0xFF);
}

TEST(BlockStreamTests, Decode_rejects_an_archive_and_decode_frame_reads_its_data)
{
    compressor::BlockEncoder encoder(engine_factory(), block_size_log2, 1);
    std::istringstream text("archived bytes");
    std::ostringstream archive;
    ASSERT_TRUE(encoder.encode_frame(text, archive, compressor::archive_flag));
    const std::string bytes = archive.str() + "index follows";

    EXPECT_EQ(decode(bytes), std::unexpected(compressor::Error::unexpected_archive));

    compressor::BlockDecoder decoder(engine_factory(), 1);
    std::istringstream in(bytes);
    std::ostringstream out;
    ASSERT_TRUE(decoder.decode_frame(in, out, compressor::archive_flag));
    EXPECT_EQ(out.str(), "archived bytes");
    EXPECT_EQ(in.tellg(), static_cast<std::streamoff>(archive.str().size()));
}

TEST(BlockStreamTests, Decode_frame_rejects_a_plain_container_when_an_archive_is_expected)
{
    const auto bytes = encode("plain");
    ASSERT_TRUE(bytes);
    compressor::BlockDecoder decoder(engine_factory(), 1);
    std::istringstream in(*bytes);
    std::ostringstream out;

    EXPECT_EQ(decoder.decode_frame(in, out, compressor::archive_flag),
              std::unexpected(compressor::Error::corrupt_header));
}

class DecodeBlocksTests : public ::testing::TestWithParam<unsigned>
{
protected:
    void SetUp() override
    {
        compressor::BlockEncoder encoder(engine_factory(), block_size_log2, 2);
        std::istringstream in(text_);
        std::ostringstream out;
        auto layout = encoder.encode_frame(in, out, compressor::archive_flag);
        ASSERT_TRUE(layout);
        bytes_ = out.str();
        offsets_ = layout->block_offsets;
        offsets_.push_back(layout->size - compressor::end_block_bytes);
    }

    std::expected<std::string, compressor::Error> decode_blocks(std::vector<uint64_t> offsets)
    {
        compressor::BlockDecoder decoder(engine_factory(), GetParam());
        std::istringstream in(bytes_);
        std::ostringstream out;
        if (auto decoded = decoder.decode_blocks(in, out, block_size_log2, offsets); !decoded)
        {
            return std::unexpected(decoded.error());
        }
        if (!offsets.empty())
        {
            EXPECT_EQ(in.tellg(), static_cast<std::streamoff>(offsets.back()));
        }
        return out.str();
    }

    // Blocks first to end - 1, and where block `end` (or the end block) starts.
    std::vector<uint64_t> range(std::size_t first, std::size_t end) const
    {
        return {offsets_.begin() + static_cast<std::ptrdiff_t>(first),
                offsets_.begin() + static_cast<std::ptrdiff_t>(end + 1)};
    }

    std::string text_ = sample_text((5 * block_size) + 9);
    std::string bytes_;
    std::vector<uint64_t> offsets_; // each block's, then the end block's
};

TEST_P(DecodeBlocksTests, A_range_of_blocks_decodes_to_its_slice_of_the_data)
{
    EXPECT_EQ(decode_blocks(range(1, 3)), text_.substr(block_size, 2 * block_size));
    EXPECT_EQ(decode_blocks(range(4, 6)), text_.substr(4 * block_size));
    EXPECT_EQ(decode_blocks(range(0, 6)), text_);
}

TEST_P(DecodeBlocksTests, No_offsets_decode_nothing)
{
    EXPECT_EQ(decode_blocks({}), "");
}

TEST_P(DecodeBlocksTests, A_block_that_does_not_end_at_the_next_offset_is_rejected)
{
    std::vector<uint64_t> offsets = range(1, 3);
    ++offsets[1];
    EXPECT_EQ(decode_blocks(offsets), std::unexpected(compressor::Error::corrupt_index));

    offsets = range(1, 3);
    ++offsets.back();
    EXPECT_EQ(decode_blocks(offsets), std::unexpected(compressor::Error::corrupt_index));
}

TEST_P(DecodeBlocksTests, A_range_past_the_last_block_is_rejected)
{
    std::vector<uint64_t> offsets = range(5, 6);
    offsets.push_back(offsets.back() + compressor::end_block_bytes);
    EXPECT_FALSE(decode_blocks(offsets));
}

INSTANTIATE_TEST_SUITE_P(Threads, DecodeBlocksTests, ::testing::Values(1U, 3U));

namespace
{

compressor::BlockEncoder single_block_encoder(uint8_t block_size_log2 = 12)
{
    return {[] {
        return std::make_unique<compressor::Engine>(12, 6);
    }, block_size_log2, 2, true};
}

std::string encode_with(compressor::BlockEncoder& encoder, const std::string& input)
{
    std::istringstream in(input);
    std::ostringstream out;
    EXPECT_TRUE(encoder.encode(in, out));
    return out.str();
}

std::expected<std::string, compressor::Error> decode_string(const std::string& encoded)
{
    compressor::BlockDecoder decoder([] {
        return std::make_unique<compressor::Engine>();
    }, 2);
    std::istringstream in(encoded);
    std::ostringstream out;
    if (auto decoded = decoder.decode(in, out); !decoded)
    {
        return std::unexpected(decoded.error());
    }
    return out.str();
}

} // namespace

TEST(BlockStreamTests, An_input_that_fits_one_block_takes_the_single_block_frame)
{
    auto encoder = single_block_encoder();
    for (const std::string& input :
         {std::string("a"), std::string(300, 'x'), std::string("abc") + std::string(4093, 'q')})
    {
        const std::string encoded = encode_with(encoder, input);
        ASSERT_GE(encoded.size(), compressor::frame_header_bytes);
        EXPECT_EQ(static_cast<uint8_t>(encoded[6]), compressor::single_block_flag) << input.size();
        EXPECT_EQ(decode_string(encoded), input);
        std::istringstream in(encoded);
        const auto data = compressor::read_encoded(in);
        ASSERT_TRUE(data);
        EXPECT_EQ(data->blocks_.size(), 1U);
    }
    // A stored block: frame header, method, 1-byte size, CRC32, the bytes.
    compressor::BlockEncoder level1([] {
        return std::make_unique<compressor::Engine>(12, 1);
    }, 12, 1, true);
    const std::string noise_like = "\x01\x7f\x33\xc4\x90\x02\xee\x5a";
    EXPECT_EQ(encode_with(level1, noise_like).size(),
              compressor::frame_header_bytes + 1 + 1 + 4 + noise_like.size());
}

TEST(BlockStreamTests, Larger_and_empty_inputs_keep_the_normal_frame)
{
    auto encoder = single_block_encoder();
    for (const std::string& input : {std::string(), std::string(4097, 'y'), std::string(4096, 'z')})
    {
        const std::string encoded = encode_with(encoder, input);
        const bool single = input.size() == 4096;
        EXPECT_EQ(static_cast<uint8_t>(encoded[6]), single ? compressor::single_block_flag : 0)
            << input.size();
        EXPECT_EQ(decode_string(encoded), input);
    }
}

TEST(BlockStreamTests, A_damaged_single_block_frame_is_rejected)
{
    auto encoder = single_block_encoder();
    const std::string input =
        "single block frames end with their payload. " + std::string(200, '!');
    const std::string encoded = encode_with(encoder, input);
    for (std::size_t cut = 0; cut < encoded.size(); ++cut)
    {
        EXPECT_FALSE(decode_string(encoded.substr(0, cut))) << cut;
    }
    EXPECT_FALSE(decode_string(encoded + "x"));
    std::string zero_size = encoded;
    zero_size[9] = 0; // the size varint: 0
    EXPECT_FALSE(decode_string(zero_size));
    std::string archive = encoded;
    archive[6] = static_cast<char>(compressor::single_block_flag | compressor::archive_flag);
    EXPECT_EQ(decode_string(archive), std::unexpected(compressor::Error::corrupt_header));
    std::string bad_method = encoded;
    bad_method[8] = 3;
    EXPECT_EQ(decode_string(bad_method), std::unexpected(compressor::Error::corrupt_header));
}
