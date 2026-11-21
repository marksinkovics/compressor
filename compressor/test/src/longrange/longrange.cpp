#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include <compressor/data/data.h>
#include <compressor/engine/engine.h>
#include <compressor/error.h>
#include <compressor/longrange/longrange.h>
#include <compressor/stream/decoder.h>
#include <compressor/stream/encoder.h>

namespace
{

using Bytes = std::vector<uint8_t>;
using compressor::Reference;

constexpr uint8_t block_log2 = compressor::min_block_size_log2; // 4 KiB
constexpr std::size_t block_size = std::size_t{1} << block_log2;
constexpr uint8_t window_log2 = 16; // 16 blocks

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

Bytes words(std::size_t size, uint32_t seed)
{
    static const std::vector<std::string_view> list{
        "block ", "frame ", "copy ",  "earlier ",   "literal ", "window ",
        "chunk ", "hash ",  "match ", "reference ", "version ", "edit\n"};
    std::string text;
    while (text.size() < size)
    {
        seed = (seed * 1103515245U) + 12345U;
        text += list[(seed >> 16U) % list.size()];
    }
    text.resize(size);
    return {text.begin(), text.end()};
}

// Three versions of a 12 KiB text, each a few edits from the one before: repeats 3 blocks back.
Bytes versions()
{
    Bytes version = words(3 * block_size, 1);
    Bytes all = version;
    for (uint32_t edit = 0; edit < 2; ++edit)
    {
        for (const std::size_t at : {std::size_t{1000}, std::size_t{6000}, std::size_t{9000}})
        {
            const Bytes inserted = noise(20 + edit, at + edit);
            version.insert(version.begin() +
                               static_cast<std::ptrdiff_t>(at + (std::size_t{edit} * 50)),
                           inserted.begin(), inserted.end());
        }
        version.resize(3 * block_size);
        all.insert(all.end(), version.begin(), version.end());
    }
    return all;
}

std::span<const uint8_t> block_of(const Bytes& bytes, std::size_t index)
{
    const std::size_t start = index * block_size;
    return std::span(bytes).subspan(start, std::min(block_size, bytes.size() - start));
}

compressor::EngineFactory engines()
{
    return [] {
        return std::make_unique<compressor::Engine>(block_log2, 6);
    };
}

// Encodes like BlockEncoder: a references block when the matcher found copies.
compressor::EncodedBlock encode(compressor::IEngine& engine, std::span<const uint8_t> input,
                                const compressor::LongRangeMatcher::Split& split)
{
    if (split.references.empty())
    {
        return engine.encode_block(input);
    }
    const compressor::EncodedBlock literals = engine.encode_block(split.literals);
    compressor::EncodedBlock block = engine.encode_block(input);
    block.method_ = compressor::Method::references;
    block.code_lengths_ = {};
    block.payload_ = compressor::write_references(split.references, literals);
    return block;
}

std::string encode_stream(const Bytes& input, unsigned threads, uint8_t window = window_log2)
{
    compressor::BlockEncoder encoder(engines(), block_log2, threads, false, window);
    std::istringstream in(std::string(input.begin(), input.end()));
    std::ostringstream out;
    EXPECT_TRUE(encoder.encode(in, out));
    return out.str();
}

std::expected<Bytes, compressor::Error> decode_stream(const std::string& encoded, unsigned threads)
{
    compressor::BlockDecoder decoder(engines(), threads);
    std::istringstream in(encoded);
    std::ostringstream out;
    if (auto decoded = decoder.decode(in, out); !decoded)
    {
        return std::unexpected(decoded.error());
    }
    const std::string text = out.str();
    return Bytes(text.begin(), text.end());
}

std::size_t references_blocks(const std::string& encoded)
{
    std::istringstream in(encoded);
    compressor::FrameReader reader(in);
    EXPECT_TRUE(reader.read_header());
    compressor::EncodedBlock block;
    std::size_t count = 0;
    while (true)
    {
        const auto more = reader.read_block(block);
        EXPECT_TRUE(more);
        if (!more || !*more)
        {
            return count;
        }
        count += static_cast<std::size_t>(block.method_ == compressor::Method::references);
    }
}

} // namespace

TEST(LongRangeTests, References_round_trip_through_the_payload)
{
    compressor::Engine engine(block_log2, 6);
    const std::vector<Reference> references{{.literals = 3, .back = 1, .offset = 10, .length = 40},
                                            {.literals = 0, .back = 200, .offset = 0, .length = 1}};
    for (const Bytes& literals : {words(3000, 2), noise(500, 3), Bytes(2000, 'x')})
    {
        const compressor::EncodedBlock inner = engine.encode_block(literals);
        std::vector<Reference> read;
        compressor::EncodedBlock read_inner;
        ASSERT_TRUE(compressor::read_references(compressor::write_references(references, inner),
                                                read, read_inner));
        EXPECT_EQ(read, references);
        EXPECT_EQ(read_inner, inner);
    }
}

TEST(LongRangeTests, Repeats_in_earlier_blocks_become_references)
{
    const Bytes input = versions();
    compressor::LongRangeMatcher matcher(compressor::window_blocks(block_log2, window_log2));
    compressor::LongRangeHistory history(compressor::window_blocks(block_log2, window_log2));
    compressor::Engine engine(block_log2, 6);
    Bytes decoded;
    std::size_t copied = 0;
    for (std::size_t i = 0; i * block_size < input.size(); ++i)
    {
        const std::span<const uint8_t> block = block_of(input, i);
        const compressor::LongRangeMatcher::Split split = matcher.next(block);
        if (i < 3)
        {
            EXPECT_TRUE(split.references.empty()) << i;
        }
        for (const Reference& reference : split.references)
        {
            copied += reference.length;
        }
        ASSERT_TRUE(history.decode(engine, encode(engine, block, split), decoded)) << i;
    }
    EXPECT_EQ(decoded, input);
    EXPECT_GT(copied, 6 * block_size * 9 / 10);
}

TEST(LongRangeTests, Nothing_is_copied_from_beyond_the_window)
{
    Bytes input = noise(2 * block_size, 4);
    const Bytes first = noise(block_size, 4);
    input.insert(input.end(), first.begin(), first.end());
    for (const uint64_t window : {uint64_t{1}, uint64_t{2}})
    {
        compressor::LongRangeMatcher matcher(window);
        matcher.next(block_of(input, 0));
        matcher.next(block_of(input, 1));
        const auto split = matcher.next(block_of(input, 2));
        if (window == 1)
        {
            EXPECT_TRUE(split.references.empty());
        }
        else
        {
            ASSERT_EQ(split.references.size(), 1U);
            EXPECT_EQ(split.references[0],
                      (Reference{.literals = 0, .back = 2, .offset = 0, .length = block_size}));
            EXPECT_TRUE(split.literals.empty());
        }
    }
}

TEST(LongRangeTests, Damaged_references_fail_cleanly)
{
    compressor::Engine engine(block_log2, 6);
    const Bytes literals = words(1000, 5);
    const std::vector<Reference> references{
        {.literals = 100, .back = 1, .offset = 20, .length = 300}};
    const Bytes payload = compressor::write_references(references, engine.encode_block(literals));
    std::vector<Reference> read;
    compressor::EncodedBlock inner;
    for (std::size_t length = 0; length < 12; ++length)
    {
        EXPECT_EQ(compressor::read_references(std::span(payload).first(length), read, inner),
                  std::unexpected(compressor::Error::corrupt_payload))
            << length;
    }
    Bytes no_references = payload;
    no_references[0] = 0;
    EXPECT_FALSE(compressor::read_references(no_references, read, inner));
    for (uint32_t seed = 0; seed < 300; ++seed)
    {
        const Bytes garbage = noise(10 + (seed % 200), seed);
        if (compressor::read_references(garbage, read, inner))
        {
            Bytes out;
            const auto decoded = engine.decode_block(inner, out);
            EXPECT_TRUE(decoded || decoded.error() != compressor::Error::write_failed);
        }
    }

    // Rebuilding: the target block is the one before, 2000 bytes of noise.
    const Bytes target = noise(2000, 6);
    Bytes expected(literals.begin(), literals.begin() + 100);
    expected.insert(expected.end(), target.begin() + 20, target.begin() + 320);
    expected.insert(expected.end(), literals.begin() + 100, literals.end());
    compressor::EncodedBlock block{.method_ = compressor::Method::references,
                                   .size_ = static_cast<uint32_t>(expected.size()),
                                   .crc_ = engine.encode_block(expected).crc_,
                                   .code_lengths_ = {},
                                   .payload_ = {}};
    const auto rebuild = [&](std::span<const Reference> with, const compressor::EncodedBlock& b) {
        Bytes out{0x55};
        auto result = compressor::rebuild_block(b, with, literals, [&](uint32_t back) {
            return back == 1 ? std::expected<std::span<const uint8_t>, compressor::Error>(target)
                             : std::unexpected(compressor::Error::corrupt_payload);
        }, out);
        EXPECT_EQ(out.size(), result ? expected.size() + 1 : 1U);
        return result;
    };
    EXPECT_TRUE(rebuild(references, block));
    std::vector<Reference> bad = references;
    bad[0].back = 2;
    EXPECT_EQ(rebuild(bad, block), std::unexpected(compressor::Error::corrupt_payload));
    bad = references;
    bad[0].offset = 1800;
    EXPECT_EQ(rebuild(bad, block), std::unexpected(compressor::Error::corrupt_payload));
    bad = references;
    bad[0].literals = 1001;
    EXPECT_EQ(rebuild(bad, block), std::unexpected(compressor::Error::corrupt_payload));
    compressor::EncodedBlock wrong = block;
    ++wrong.crc_;
    EXPECT_EQ(rebuild(references, wrong), std::unexpected(compressor::Error::checksum_mismatch));
    wrong = block;
    --wrong.size_;
    EXPECT_EQ(rebuild(references, wrong), std::unexpected(compressor::Error::corrupt_payload));
}

class LongRangeStreamTests : public ::testing::TestWithParam<unsigned>
{
};

TEST_P(LongRangeStreamTests, A_long_range_frame_round_trips_whatever_the_thread_count)
{
    const Bytes input = versions();
    const std::string encoded = encode_stream(input, GetParam());
    EXPECT_EQ(encoded, encode_stream(input, 1));
    ASSERT_GT(encoded.size(), compressor::frame_header_bytes);
    EXPECT_EQ(static_cast<uint8_t>(encoded[6]) & compressor::long_range_flag,
              compressor::long_range_flag);
    EXPECT_EQ(static_cast<uint8_t>(encoded[8]), window_log2);
    EXPECT_GE(references_blocks(encoded), 5U);
    EXPECT_LT(encoded.size(), encode_stream(input, 1, 0).size() * 2 / 3);
    EXPECT_EQ(decode_stream(encoded, GetParam()), input);
}

TEST_P(LongRangeStreamTests, Blocks_from_the_middle_fetch_the_blocks_they_copy_from)
{
    const Bytes input = versions();
    compressor::BlockEncoder encoder(engines(), block_log2, 2, false, window_log2);
    std::istringstream in(std::string(input.begin(), input.end()));
    std::ostringstream out;
    const auto layout = encoder.encode_frame(in, out, compressor::archive_flag);
    ASSERT_TRUE(layout);
    std::vector<uint64_t> offsets = layout->block_offsets;
    offsets.push_back(layout->size - compressor::end_block_bytes);
    const std::string encoded = out.str();
    const compressor::FrameBlocks blocks{.offsets = offsets,
                                         .block_size_log2 = block_log2,
                                         .flags =
                                             compressor::archive_flag | compressor::long_range_flag,
                                         .window_log2 = window_log2};
    for (const auto& [first, end] :
         {std::pair{std::size_t{7}, std::size_t{9}}, std::pair{std::size_t{3}, std::size_t{4}},
          std::pair{std::size_t{0}, std::size_t{9}}})
    {
        compressor::BlockDecoder decoder(engines(), GetParam());
        std::istringstream encoded_in(encoded);
        std::ostringstream decoded;
        ASSERT_TRUE(decoder.decode_blocks(encoded_in, decoded, blocks, first, end)) << first;
        EXPECT_EQ(encoded_in.tellg(), static_cast<std::streamoff>(offsets[end]));
        const std::string text = decoded.str();
        EXPECT_EQ(Bytes(text.begin(), text.end()),
                  Bytes(input.begin() + static_cast<std::ptrdiff_t>(first * block_size),
                        input.begin() + static_cast<std::ptrdiff_t>(end * block_size)))
            << first;
    }
}

INSTANTIATE_TEST_SUITE_P(Threads, LongRangeStreamTests, ::testing::Values(1U, 2U, 8U));

TEST(LongRangeStreamTests, Readers_reject_bad_windows_and_references_without_the_flag)
{
    const auto header = [](uint16_t flags, uint8_t window) {
        std::string bytes{"\x89ING"};
        bytes += {'\x02', static_cast<char>(block_log2), static_cast<char>(flags & 0xFFU),
                  static_cast<char>(flags >> 8U), static_cast<char>(window)};
        std::istringstream in(bytes);
        compressor::FrameReader reader(in);
        return reader.read_header();
    };
    EXPECT_TRUE(header(compressor::long_range_flag, block_log2));
    EXPECT_TRUE(header(compressor::long_range_flag, compressor::max_window_log2));
    for (const auto& [flags, window] :
         {std::pair{compressor::long_range_flag, uint8_t{block_log2 - 1}},
          std::pair{compressor::long_range_flag, uint8_t{compressor::max_window_log2 + 1}},
          std::pair{uint16_t{compressor::long_range_flag | compressor::single_block_flag},
                    uint8_t{20}}})
    {
        EXPECT_EQ(header(flags, window), std::unexpected(compressor::Error::corrupt_header))
            << flags << ' ' << int{window};
    }

    // A references block in a frame without the flag.
    const std::string plain = encode_stream(versions(), 1, 0);
    std::string changed = plain;
    changed[compressor::frame_header_bytes] = static_cast<char>(compressor::Method::references);
    EXPECT_EQ(decode_stream(changed, 1), std::unexpected(compressor::Error::corrupt_header));
}
