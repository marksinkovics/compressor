#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include <compressor/checksum/crc32.h>
#include <compressor/data/data.h>
#include <compressor/engine/engine.h>
#include <compressor/entropy/rans.h>
#include <compressor/error.h>
#include <compressor/transform/transform.h>

namespace
{

compressor::DecodedData decoded(std::string_view text)
{
    compressor::DecodedData data;
    data.data_.assign(text.begin(), text.end());
    return data;
}

std::span<const uint8_t> bytes(std::string_view text)
{
    return {reinterpret_cast<const uint8_t*>(text.data()), text.size()};
}

uint32_t crc(std::string_view text)
{
    return compressor::crc32(bytes(text));
}

std::expected<compressor::DecodedData, compressor::Error>
decode_block(compressor::Engine& engine, const compressor::EncodedBlock& block)
{
    compressor::DecodedData result;
    if (auto decoded = engine.decode_block(block, result.data_); !decoded)
    {
        return std::unexpected(decoded.error());
    }
    return result;
}

std::string pseudo_random_text(std::size_t size)
{
    std::string text(size, '\0');
    uint32_t state = 7;
    for (char& c : text)
    {
        state = (state * 1103515245U) + 12345U;
        c = static_cast<char>('a' + ((state >> 16U) % 7));
    }
    return text;
}

std::string repeat(std::string_view text, std::size_t count)
{
    std::string result;
    for (std::size_t i = 0; i < count; ++i)
    {
        result += text;
    }
    return result;
}

} // namespace

class EngineTests : public ::testing::Test
{
protected:
    const std::string input = "go go gophers";
    // Blocks below a few hundred bytes are stored, as the code lengths alone are 128 bytes.
    const std::string aab = repeat("aab", 201); // a = 0, b = 1: 603 bits
    const std::string longInput = R"(
        Lorem ipsum dolor sit amet, consectetur adipiscing elit.
        Curabitur id consectetur est. Nam a ligula nibh.
        Nulla feugiat posuere sollicitudin. Nulla facilisi.
        Phasellus ac tempus quam.
        Praesent tincidunt semper justo ac gravida.
        Orci varius natoque penatibus et magnis dis parturient montes, nascetur ridiculus mus.
        Donec rutrum est tortor, eget venenatis neque rutrum ac.
        Vivamus id mi semper, interdum dui eget, tempor augue.
        Sed sed magna eleifend, porttitor libero non, tempus eros.
    )";
};

TEST_F(EngineTests, Compression_with_text)
{
    compressor::Engine comp1;
    compressor::EncodedData encoded_data = comp1.encode(decoded(input));

    compressor::Engine comp2;
    const auto decoded_data = comp2.decode(encoded_data);
    ASSERT_TRUE(decoded_data.has_value());

    std::string decoded_str(decoded_data->data_.begin(), decoded_data->data_.end());

    EXPECT_STREQ(input.c_str(), decoded_str.c_str());
}

TEST_F(EngineTests, Compression_with_long_text)
{
    compressor::Engine comp1;
    compressor::EncodedData encoded_data = comp1.encode(decoded(longInput));

    compressor::Engine comp2;
    const auto decoded_data = comp2.decode(encoded_data);
    ASSERT_TRUE(decoded_data.has_value());

    std::string decoded_str(decoded_data->data_.begin(), decoded_data->data_.end());

    EXPECT_STREQ(longInput.c_str(), decoded_str.c_str());
}

TEST_F(EngineTests, Stream)
{
    std::stringstream stream;
    compressor::Engine comp1;
    compressor::EncodedData encoded_data = comp1.encode(decoded(input));

    stream << encoded_data;

    compressor::EncodedData encoded_data2;
    stream >> encoded_data2;

    compressor::Engine comp2;
    const auto decoded_data = comp2.decode(encoded_data2);
    ASSERT_TRUE(decoded_data.has_value());

    std::string decoded_str(decoded_data->data_.begin(), decoded_data->data_.end());

    EXPECT_STREQ(input.c_str(), decoded_str.c_str());
}

TEST_F(EngineTests, Encoding_twice_gives_the_same_result_as_a_fresh_engine)
{
    compressor::Engine reused;
    reused.encode(decoded(longInput));
    compressor::EncodedData second = reused.encode(decoded(input));

    compressor::Engine fresh;
    EXPECT_EQ(second, fresh.encode(decoded(input)));
}

TEST_F(EngineTests, Skewed_input_with_capped_codes_round_trips)
{
    // Fibonacci counts would need codes longer than max_code_length.
    std::string skewed;
    uint64_t a = 1;
    uint64_t b = 1;
    for (char symbol = 'a'; symbol < 'a' + 20; ++symbol)
    {
        skewed.append(a, symbol);
        const uint64_t next = a + b;
        a = b;
        b = next;
    }

    compressor::Engine encoder;
    const compressor::EncodedData encoded = encoder.encode(decoded(skewed));
    ASSERT_EQ(encoded.blocks_.size(), 1U);
    EXPECT_EQ(*std::ranges::max_element(encoded.blocks_[0].code_lengths_),
              compressor::max_code_length);

    std::stringstream stream;
    stream << encoded;
    compressor::EncodedData read_back;
    stream >> read_back;

    EXPECT_EQ(read_back, encoded);

    compressor::Engine decoder;
    const auto result = decoder.decode(read_back);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::string(result->data_.begin(), result->data_.end()), skewed);
}

TEST_F(EngineTests, Encode_packs_codes_first_bit_most_significant)
{
    // 001 repeated: 00100100 10010010 01001001, then the last 001 padded.
    std::vector<uint8_t> expected;
    for (int i = 0; i < 25; ++i)
    {
        expected.insert(expected.end(), {0x24, 0x92, 0x49});
    }
    expected.push_back(0x20);

    compressor::Engine engine;
    const compressor::EncodedBlock encoded = engine.encode_block(bytes(aab));
    EXPECT_EQ(encoded.method_, compressor::Method::huffman);
    EXPECT_EQ(encoded.payload_, expected);
}

TEST_F(EngineTests, Encode_packs_codes_across_bytes_and_pads_the_last_one)
{
    // a = 0, b = 10, c = 11, so "abac" is 010011, and four of them are 4D 34 D3.
    std::vector<uint8_t> expected;
    for (int i = 0; i < 16; ++i)
    {
        expected.insert(expected.end(), {0x4D, 0x34, 0xD3});
    }
    expected.push_back(0x4C);

    compressor::Engine engine;
    EXPECT_EQ(engine.encode_block(bytes(repeat("abac", 65))).payload_, expected);
}

TEST_F(EngineTests, A_truncated_payload_is_corrupt)
{
    compressor::Engine encoder;
    compressor::EncodedBlock encoded = encoder.encode_block(bytes(longInput));
    ASSERT_EQ(encoded.method_, compressor::Method::huffman);
    encoded.payload_.resize(encoded.payload_.size() / 2);

    compressor::Engine decoder;
    EXPECT_EQ(decode_block(decoder, encoded), std::unexpected(compressor::Error::corrupt_payload));
}

TEST_F(EngineTests, Print_dict_lists_the_canonical_codes)
{
    compressor::Engine engine;
    engine.encode(decoded(aab));

    testing::internal::CaptureStdout();
    engine.print_dict();
    EXPECT_EQ(testing::internal::GetCapturedStdout(), "0 => [a]\n1 => [b]\n");
}

TEST_F(EngineTests, Print_graph_rebuilds_the_tree_from_the_codes)
{
    compressor::Engine engine;
    engine.encode(decoded(aab));

    testing::internal::CaptureStdout();
    engine.print_graph();
    EXPECT_EQ(testing::internal::GetCapturedStdout(), "digraph G {\n"
                                                      "\t\"n\" [label=\"ab [603, ]\"];\n"
                                                      "\t\"n\" -> \"n0\";\n"
                                                      "\t\"n\" -> \"n1\";\n"
                                                      "\t\"n0\" [label=\"a [402, 0]\"];\n"
                                                      "\t\"n1\" [label=\"b [201, 1]\"];\n"
                                                      "}\n");
}

TEST_F(EngineTests, Print_graph_handles_a_single_symbol)
{
    compressor::Engine engine;
    engine.encode(decoded(std::string(200, 'a')));

    testing::internal::CaptureStdout();
    engine.print_graph();
    EXPECT_EQ(testing::internal::GetCapturedStdout(), "digraph G {\n"
                                                      "\t\"n\" [label=\"a [200, ]\"];\n"
                                                      "\t\"n\" -> \"n0\";\n"
                                                      "\t\"n0\" [label=\"a [200, 0]\"];\n"
                                                      "}\n");
}

TEST_F(EngineTests, A_bit_pattern_that_starts_no_code_is_corrupt)
{
    // A single symbol has the code 0, so no code starts with 1.
    compressor::EncodedBlock encoded;
    encoded.size_ = 2;
    encoded.code_lengths_['a'] = 1;
    encoded.payload_ = {0x40}; // 01

    compressor::Engine decoder;
    EXPECT_EQ(decode_block(decoder, encoded), std::unexpected(compressor::Error::corrupt_payload));
}

TEST_F(EngineTests, A_code_that_ends_past_the_payload_is_corrupt)
{
    // a = 0, b = 10, c = 11: after seven "a", the last bit 1 starts a two-bit code that doesn't
    // fit.
    compressor::EncodedBlock encoded;
    encoded.size_ = 8;
    encoded.code_lengths_['a'] = 1;
    encoded.code_lengths_['b'] = 2;
    encoded.code_lengths_['c'] = 2;
    encoded.payload_ = {0x01};

    compressor::Engine decoder;
    EXPECT_EQ(decode_block(decoder, encoded), std::unexpected(compressor::Error::corrupt_payload));
}

TEST_F(EngineTests, A_trailing_byte_after_the_codes_is_corrupt)
{
    compressor::Engine encoder;
    compressor::EncodedBlock encoded = encoder.encode_block(bytes(aab));
    ASSERT_EQ(encoded.method_, compressor::Method::huffman);
    encoded.payload_.push_back(0x00);

    compressor::Engine decoder;
    EXPECT_EQ(decode_block(decoder, encoded), std::unexpected(compressor::Error::corrupt_payload));
}

TEST_F(EngineTests, Non_zero_padding_is_corrupt)
{
    // a = 0, b = 1: "aab" is 001, so the last five bits are padding.
    compressor::EncodedBlock encoded;
    encoded.size_ = 3;
    encoded.code_lengths_['a'] = 1;
    encoded.code_lengths_['b'] = 1;
    encoded.payload_ = {0x21};

    compressor::Engine decoder;
    EXPECT_EQ(decode_block(decoder, encoded), std::unexpected(compressor::Error::corrupt_payload));
}

TEST_F(EngineTests, A_payload_for_empty_data_is_corrupt)
{
    compressor::EncodedBlock encoded;
    encoded.payload_ = {0x00};

    compressor::Engine decoder;
    EXPECT_EQ(decode_block(decoder, encoded), std::unexpected(compressor::Error::corrupt_payload));
}

TEST_F(EngineTests, Codes_that_fill_the_last_byte_exactly_decode)
{
    // a = 0, b = 1: 400 symbols are 50 bytes without padding.
    const std::string input = repeat("ab", 200);
    compressor::Engine encoder;
    const compressor::EncodedBlock encoded = encoder.encode_block(bytes(input));
    ASSERT_EQ(encoded.method_, compressor::Method::huffman);
    ASSERT_EQ(encoded.payload_.size(), 50U);

    compressor::Engine decoder;
    const auto result = decode_block(decoder, encoded);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::string(result->data_.begin(), result->data_.end()), input);
}

TEST_F(EngineTests, Input_that_huffman_cannot_shrink_is_stored)
{
    // Every byte value equally often: every code is 8 bits.
    compressor::DecodedData input;
    for (int i = 0; i < 512; ++i)
    {
        input.data_.push_back(static_cast<uint8_t>(i));
    }

    compressor::Engine encoder;
    const compressor::EncodedBlock encoded = encoder.encode_block(input.data_);
    EXPECT_EQ(encoded.method_, compressor::Method::stored);
    EXPECT_EQ(encoded.size_, 512U);
    EXPECT_EQ(encoded.code_lengths_, compressor::CodeLengths{});
    EXPECT_EQ(encoded.payload_, input.data_);

    compressor::Engine decoder;
    EXPECT_EQ(decode_block(decoder, encoded), input);
}

TEST_F(EngineTests, Short_input_is_stored_because_the_huffman_header_costs_more)
{
    compressor::Engine encoder;
    const compressor::EncodedBlock encoded = encoder.encode_block(bytes(input));
    EXPECT_EQ(encoded.method_, compressor::Method::stored);

    compressor::Engine decoder;
    EXPECT_EQ(decode_block(decoder, encoded), decoded(input));
}

TEST_F(EngineTests, Empty_input_has_no_blocks_and_round_trips)
{
    compressor::Engine encoder;
    const compressor::EncodedData encoded = encoder.encode(decoded(""));
    EXPECT_TRUE(encoded.blocks_.empty());

    compressor::Engine decoder;
    EXPECT_EQ(decoder.decode(encoded), decoded(""));
}

TEST_F(EngineTests, A_tie_is_stored_and_one_byte_saved_is_huffman)
{
    // a = 0, b = 1. 147 bytes: 147 = 128 + 19. 148 bytes: 148 > 128 + 19.
    compressor::Engine engine;
    EXPECT_EQ(engine.encode_block(bytes(repeat("ab", 74).substr(0, 147))).method_,
              compressor::Method::stored);
    EXPECT_EQ(engine.encode_block(bytes(repeat("ab", 74))).method_, compressor::Method::huffman);
}

TEST_F(EngineTests, Print_dict_and_print_graph_are_empty_after_storing)
{
    compressor::Engine engine;
    engine.encode(decoded(input));

    testing::internal::CaptureStdout();
    engine.print_dict();
    engine.print_graph();
    EXPECT_EQ(testing::internal::GetCapturedStdout(), "digraph G {\n}\n");
}

TEST_F(EngineTests, A_stored_payload_of_the_wrong_size_is_corrupt)
{
    compressor::EncodedBlock encoded;
    encoded.method_ = compressor::Method::stored;
    encoded.size_ = 3;

    compressor::Engine decoder;
    encoded.payload_ = {'a', 'b'};
    EXPECT_EQ(decode_block(decoder, encoded), std::unexpected(compressor::Error::corrupt_payload));
    encoded.payload_ = {'a', 'b', 'c', 'd'};
    EXPECT_EQ(decode_block(decoder, encoded), std::unexpected(compressor::Error::corrupt_payload));
}

TEST_F(EngineTests, Encoded_blocks_carry_the_crc32_of_their_bytes)
{
    compressor::Engine engine;
    EXPECT_EQ(engine.encode_block(bytes(aab)).crc_, crc(aab));
    EXPECT_EQ(engine.encode_block(bytes(input)).crc_, crc(input));
}

TEST_F(EngineTests, Input_is_split_into_blocks_of_the_block_size)
{
    constexpr std::size_t block_size = 4096;
    for (const std::size_t size :
         {1UZ, block_size - 1, block_size, block_size + 1, 3 * block_size, (3 * block_size) + 5})
    {
        const std::string text = pseudo_random_text(size);
        compressor::Engine encoder(12);
        const compressor::EncodedData encoded = encoder.encode(decoded(text));
        EXPECT_EQ(encoded.block_size_log2_, 12U);
        ASSERT_EQ(encoded.blocks_.size(), (size + block_size - 1) / block_size) << size;
        for (std::size_t i = 0; i < encoded.blocks_.size(); ++i)
        {
            const std::size_t expected = std::min(block_size, size - (i * block_size));
            EXPECT_EQ(encoded.blocks_[i].size_, expected) << size << " block " << i;
            EXPECT_EQ(encoded.blocks_[i].crc_,
                      crc(std::string_view(text).substr(i * block_size, expected)));
        }

        compressor::Engine decoder;
        EXPECT_EQ(decoder.decode(encoded), decoded(text)) << size;
    }
}

TEST_F(EngineTests, Each_block_chooses_its_own_method)
{
    // A compressible block, then one with every byte value equally often.
    std::string text = repeat("aab", 1366).substr(0, 4096);
    for (int i = 0; i < 4096; ++i)
    {
        text.push_back(static_cast<char>(i));
    }
    compressor::Engine encoder(12);
    const compressor::EncodedData encoded = encoder.encode(decoded(text));
    ASSERT_EQ(encoded.blocks_.size(), 2U);
    EXPECT_EQ(encoded.blocks_[0].method_, compressor::Method::huffman);
    EXPECT_EQ(encoded.blocks_[1].method_, compressor::Method::stored);

    compressor::Engine decoder;
    EXPECT_EQ(decoder.decode(encoded), decoded(text));
}

TEST_F(EngineTests, A_changed_stored_byte_fails_the_checksum)
{
    compressor::Engine encoder;
    compressor::EncodedBlock encoded = encoder.encode_block(bytes(input));
    ASSERT_EQ(encoded.method_, compressor::Method::stored);
    encoded.payload_[3] ^= 0x01U;

    compressor::Engine decoder;
    EXPECT_EQ(decode_block(decoder, encoded),
              std::unexpected(compressor::Error::checksum_mismatch));
}

TEST_F(EngineTests, A_huffman_block_with_a_wrong_crc_fails_the_checksum)
{
    compressor::Engine encoder;
    compressor::EncodedBlock encoded = encoder.encode_block(bytes(aab));
    ASSERT_EQ(encoded.method_, compressor::Method::huffman);
    encoded.crc_ ^= 0x80000000U;

    compressor::Engine decoder;
    EXPECT_EQ(decode_block(decoder, encoded),
              std::unexpected(compressor::Error::checksum_mismatch));
}

TEST_F(EngineTests, A_failed_block_leaves_the_output_unchanged)
{
    compressor::Engine encoder;
    compressor::EncodedBlock encoded = encoder.encode_block(bytes(aab));
    encoded.crc_ ^= 1U;

    compressor::Engine decoder;
    std::vector<uint8_t> out{1, 2, 3};
    EXPECT_FALSE(decoder.decode_block(encoded, out).has_value());
    EXPECT_EQ(out, (std::vector<uint8_t>{1, 2, 3}));
}

TEST_F(EngineTests, A_corrupt_later_block_fails_the_whole_decode)
{
    const std::string text = pseudo_random_text(3UZ * 4096);
    compressor::Engine encoder(12);
    compressor::EncodedData encoded = encoder.encode(decoded(text));
    ASSERT_EQ(encoded.blocks_.size(), 3U);
    encoded.blocks_[2].crc_ ^= 1U;

    compressor::Engine decoder;
    EXPECT_EQ(decoder.decode(encoded), std::unexpected(compressor::Error::checksum_mismatch));
}

TEST_F(EngineTests, From_level_2_repetitive_text_becomes_an_lz77_huffman_pipeline_block)
{
    const std::string text = repeat("the same sentence, over and over again. ", 2000);
    compressor::Engine fast(compressor::default_block_size_log2, 1);
    const compressor::EncodedBlock huffman = fast.encode_block(bytes(text));
    EXPECT_EQ(huffman.method_, compressor::Method::huffman);

    for (const unsigned level : {2U, 6U})
    {
        compressor::Engine engine(compressor::default_block_size_log2, level);
        const compressor::EncodedBlock block = engine.encode_block(bytes(text));
        ASSERT_EQ(block.method_, compressor::Method::pipeline) << level;
        EXPECT_EQ(block.payload_[0], 0) << "no transforms";
        EXPECT_EQ(block.payload_[1], static_cast<uint8_t>(compressor::Coder::lz77_huffman));
        EXPECT_LT(block.payload_.size(), huffman.payload_.size() / 20) << level;
        EXPECT_EQ(decode_block(engine, block), decoded(text)) << level;
    }
}

TEST_F(EngineTests, Random_bytes_stay_stored_at_every_level)
{
    std::string bytes_noise(20'000, '\0');
    uint32_t state = 1;
    for (char& c : bytes_noise)
    {
        state = (state * 1103515245U) + 12345U;
        c = static_cast<char>(state >> 16U);
    }
    compressor::Engine engine(compressor::default_block_size_log2, 9);
    EXPECT_EQ(engine.encode_block(bytes(bytes_noise)).method_, compressor::Method::stored);
}

TEST_F(EngineTests, A_pipeline_with_an_unknown_transform_or_coder_is_corrupt)
{
    // Sentences: no transform is chosen, so the descriptor is just the count 0 and the coder.
    const std::string text = repeat("the quick brown fox jumps over the lazy dog. ", 300);
    compressor::Engine engine(compressor::default_block_size_log2, 6);
    compressor::EncodedBlock block = engine.encode_block(bytes(text));
    ASSERT_EQ(block.method_, compressor::Method::pipeline);
    ASSERT_EQ(block.payload_[0], 0);

    compressor::EncodedBlock unknown_transform = block;
    unknown_transform.payload_[0] = 1;
    EXPECT_EQ(decode_block(engine, unknown_transform),
              std::unexpected(compressor::Error::corrupt_payload));
    compressor::EncodedBlock unknown_coder = block;
    unknown_coder.payload_[1] = 7;
    EXPECT_EQ(decode_block(engine, unknown_coder),
              std::unexpected(compressor::Error::corrupt_payload));
    compressor::EncodedBlock wrong_crc = block;
    wrong_crc.crc_ ^= 1U;
    EXPECT_EQ(decode_block(engine, wrong_crc),
              std::unexpected(compressor::Error::checksum_mismatch));
}

TEST_F(EngineTests, Sixteen_bit_samples_get_a_delta_transform_and_round_trip)
{
    // A random walk with small steps in little-endian 16-bit samples: deltas of stride 2 are
    // small, and nothing repeats at a longer distance (as rows of an image would).
    std::string samples;
    uint32_t state = 7;
    int32_t value = 30'000;
    for (int i = 0; i < 100'000; ++i)
    {
        state = (state * 1103515245U) + 12345U;
        value += static_cast<int32_t>((state >> 16U) % 15) - 7;
        samples.push_back(static_cast<char>(static_cast<uint16_t>(value) & 0xFFU));
        samples.push_back(static_cast<char>(static_cast<uint16_t>(value) >> 8U));
    }
    for (const unsigned level : {6U, compressor::auto_level})
    {
        compressor::Engine engine(compressor::default_block_size_log2, level);
        const compressor::EncodedBlock block = engine.encode_block(bytes(samples));
        ASSERT_EQ(block.method_, compressor::Method::pipeline);
        EXPECT_EQ(block.payload_[0], 1) << "one transform";
        EXPECT_EQ(block.payload_[1], static_cast<uint8_t>(compressor::TransformId::delta));
        EXPECT_EQ(decode_block(engine, block), decoded(samples)) << level;
    }
}

TEST_F(EngineTests, A_transform_with_an_invalid_parameter_is_corrupt)
{
    std::string samples;
    for (int i = 0; i < 50'000; ++i)
    {
        samples.push_back(static_cast<char>(i));
        samples.push_back(static_cast<char>(i / 256));
    }
    compressor::Engine engine(compressor::default_block_size_log2, 6);
    compressor::EncodedBlock block = engine.encode_block(bytes(samples));
    ASSERT_EQ(block.method_, compressor::Method::pipeline);
    ASSERT_EQ(block.payload_[0], 1);
    compressor::EncodedBlock zero_stride = block;
    std::fill_n(zero_stride.payload_.begin() + 2, 4, 0);
    EXPECT_EQ(decode_block(engine, zero_stride),
              std::unexpected(compressor::Error::corrupt_payload));
    compressor::EncodedBlock too_many = block;
    too_many.payload_[0] = 5;
    EXPECT_EQ(decode_block(engine, too_many), std::unexpected(compressor::Error::corrupt_payload));
}

namespace
{

std::string bwt_words()
{
    std::string text;
    uint32_t state = 3;
    while (text.size() < 300'000)
    {
        state = (state * 1103515245U) + 12345U;
        static constexpr std::array<std::string_view, 6> words{"sorted ", "suffix ", "block ",
                                                               "front ",  "move ",   "rows.\n"};
        text += words[(state >> 16U) % words.size()];
    }
    return text;
}

} // namespace

TEST_F(EngineTests, At_level_8_text_can_take_the_bwt_coder)
{
    const std::string text = bwt_words();
    compressor::Engine engine(compressor::default_block_size_log2, compressor::bwt_level);
    const compressor::EncodedBlock block = engine.encode_block(bytes(text));
    ASSERT_EQ(block.method_, compressor::Method::pipeline);
    EXPECT_EQ(block.payload_[(block.payload_[0] * 5) + 1],
              static_cast<uint8_t>(compressor::Coder::bwt));
    EXPECT_EQ(decode_block(engine, block), decoded(text));
}

TEST_F(EngineTests, At_level_9_text_takes_the_context_mixing_coder)
{
    const std::string text = bwt_words();
    compressor::Engine engine(compressor::default_block_size_log2, compressor::cm_level);
    const compressor::EncodedBlock block = engine.encode_block(bytes(text));
    ASSERT_EQ(block.method_, compressor::Method::pipeline);
    EXPECT_EQ(block.payload_[(block.payload_[0] * 5) + 1],
              static_cast<uint8_t>(compressor::Coder::cm));
    EXPECT_EQ(decode_block(engine, block), decoded(text));

    compressor::EncodedBlock damaged = block;
    damaged.payload_.pop_back();
    EXPECT_EQ(decode_block(engine, damaged), std::unexpected(compressor::Error::corrupt_payload));
}

TEST_F(EngineTests, Noisy_samples_take_a_delta_and_the_tans_coder)
{
    // A random walk with wide steps: after a delta, noise that LZ77 can't match.
    std::string samples;
    uint32_t state = 5;
    int32_t value = 30'000;
    for (int i = 0; i < 200'000; ++i)
    {
        state = (state * 1103515245U) + 12345U;
        value += static_cast<int32_t>((state >> 16U) % 1023) - 511;
        samples.push_back(static_cast<char>(static_cast<uint16_t>(value)));
        samples.push_back(static_cast<char>(static_cast<uint16_t>(value) >> 8U));
    }
    compressor::Engine engine(compressor::default_block_size_log2, 6);
    const compressor::EncodedBlock block = engine.encode_block(bytes(samples));
    ASSERT_EQ(block.method_, compressor::Method::pipeline);
    ASSERT_EQ(block.payload_[0], 1);
    EXPECT_EQ(block.payload_[6], static_cast<uint8_t>(compressor::Coder::tans));
    EXPECT_EQ(decode_block(engine, block), decoded(samples));

    // rANS codes the same within 0.5%, and blocks written with it before tANS still decode.
    compressor::EncodedBlock rans = block;
    rans.payload_.resize(6);
    rans.payload_.push_back(static_cast<uint8_t>(compressor::Coder::rans));
    std::vector<uint8_t> delta(samples.begin(), samples.end());
    compressor::apply_transform({.id = compressor::TransformId::delta, .parameter = 2}, delta);
    ASSERT_EQ(block.payload_[1], static_cast<uint8_t>(compressor::TransformId::delta));
    ASSERT_EQ(block.payload_[2], 2);
    compressor::rans_encode(std::vector<uint16_t>(delta.begin(), delta.end()), 256, rans.payload_);
    EXPECT_LT(block.payload_.size(), rans.payload_.size() + (rans.payload_.size() / 200));
    EXPECT_EQ(decode_block(engine, rans), decoded(samples));

    // Level 9 tries the context models after the same transform, and wins with BWT + CM.
    compressor::Engine best(compressor::default_block_size_log2, compressor::cm_level);
    const compressor::EncodedBlock mixed = best.encode_block(bytes(samples));
    ASSERT_EQ(mixed.method_, compressor::Method::pipeline);
    ASSERT_EQ(mixed.payload_[0], 1);
    EXPECT_EQ(mixed.payload_[6], static_cast<uint8_t>(compressor::Coder::bwt_cm));
    EXPECT_LT(mixed.payload_.size(), block.payload_.size());
    EXPECT_EQ(decode_block(best, mixed), decoded(samples));
}

TEST_F(EngineTests, At_level_7_far_repeats_take_the_lzt_coder)
{
    // Noise repeated beyond Deflate's 32 KiB window: only LZT reaches it at level 7.
    std::string chunk(100'000, '\0');
    uint32_t state = 21;
    for (char& c : chunk)
    {
        state = (state * 1103515245U) + 12345U;
        c = static_cast<char>(state >> 16U);
    }
    const std::string input = chunk + chunk;
    compressor::Engine engine(compressor::default_block_size_log2, compressor::auto_level);
    const compressor::EncodedBlock block = engine.encode_block(bytes(input));
    ASSERT_EQ(block.method_, compressor::Method::pipeline);
    EXPECT_EQ(block.payload_[(block.payload_[0] * 5) + 1],
              static_cast<uint8_t>(compressor::Coder::lzt));
    EXPECT_LT(block.payload_.size(), chunk.size() + 2'000);
    EXPECT_EQ(decode_block(engine, block), decoded(input));

    for (const unsigned level : {6U, compressor::bwt_level})
    {
        compressor::Engine other(compressor::default_block_size_log2, level);
        const compressor::EncodedBlock coded = other.encode_block(bytes(input));
        if (coded.method_ == compressor::Method::pipeline)
        {
            EXPECT_NE(coded.payload_[(coded.payload_[0] * 5) + 1],
                      static_cast<uint8_t>(compressor::Coder::lzt))
                << level;
        }
    }
}
