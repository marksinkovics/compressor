#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include <compressor/data/data.h>
#include <compressor/engine/engine.h>
#include <compressor/error.h>

namespace
{

compressor::DecodedData decoded(std::string_view text)
{
    compressor::DecodedData data;
    data.data_.assign(text.begin(), text.end());
    return data;
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
    // Inputs below a few hundred bytes are stored, as the Huffman header alone is 137 bytes.
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
    EXPECT_EQ(*std::ranges::max_element(encoded.code_lengths_), compressor::max_code_length);

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
    const compressor::EncodedData encoded = engine.encode(decoded(aab));
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
    EXPECT_EQ(engine.encode(decoded(repeat("abac", 65))).payload_, expected);
}

TEST_F(EngineTests, A_truncated_payload_is_corrupt)
{
    compressor::Engine encoder;
    compressor::EncodedData encoded = encoder.encode(decoded(longInput));
    ASSERT_EQ(encoded.method_, compressor::Method::huffman);
    encoded.payload_.resize(encoded.payload_.size() / 2);

    compressor::Engine decoder;
    EXPECT_EQ(decoder.decode(encoded), std::unexpected(compressor::Error::corrupt_payload));
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
    compressor::EncodedData encoded;
    encoded.size_ = 2;
    encoded.code_lengths_['a'] = 1;
    encoded.payload_ = {0x40}; // 01

    compressor::Engine decoder;
    EXPECT_EQ(decoder.decode(encoded), std::unexpected(compressor::Error::corrupt_payload));
}

TEST_F(EngineTests, A_code_that_ends_past_the_payload_is_corrupt)
{
    // a = 0, b = 10, c = 11: after seven "a", the last bit 1 starts a two-bit code that doesn't
    // fit.
    compressor::EncodedData encoded;
    encoded.size_ = 8;
    encoded.code_lengths_['a'] = 1;
    encoded.code_lengths_['b'] = 2;
    encoded.code_lengths_['c'] = 2;
    encoded.payload_ = {0x01};

    compressor::Engine decoder;
    EXPECT_EQ(decoder.decode(encoded), std::unexpected(compressor::Error::corrupt_payload));
}

TEST_F(EngineTests, A_trailing_byte_after_the_codes_is_corrupt)
{
    compressor::Engine encoder;
    compressor::EncodedData encoded = encoder.encode(decoded(aab));
    ASSERT_EQ(encoded.method_, compressor::Method::huffman);
    encoded.payload_.push_back(0x00);

    compressor::Engine decoder;
    EXPECT_EQ(decoder.decode(encoded), std::unexpected(compressor::Error::corrupt_payload));
}

TEST_F(EngineTests, Non_zero_padding_is_corrupt)
{
    // a = 0, b = 1: "aab" is 001, so the last five bits are padding.
    compressor::EncodedData encoded;
    encoded.size_ = 3;
    encoded.code_lengths_['a'] = 1;
    encoded.code_lengths_['b'] = 1;
    encoded.payload_ = {0x21};

    compressor::Engine decoder;
    EXPECT_EQ(decoder.decode(encoded), std::unexpected(compressor::Error::corrupt_payload));
}

TEST_F(EngineTests, A_payload_for_empty_data_is_corrupt)
{
    compressor::EncodedData encoded;
    encoded.payload_ = {0x00};

    compressor::Engine decoder;
    EXPECT_EQ(decoder.decode(encoded), std::unexpected(compressor::Error::corrupt_payload));
}

TEST_F(EngineTests, Codes_that_fill_the_last_byte_exactly_decode)
{
    // a = 0, b = 1: 400 symbols are 50 bytes without padding.
    const std::string input = repeat("ab", 200);
    compressor::Engine encoder;
    const compressor::EncodedData encoded = encoder.encode(decoded(input));
    ASSERT_EQ(encoded.method_, compressor::Method::huffman);
    ASSERT_EQ(encoded.payload_.size(), 50U);

    compressor::Engine decoder;
    const auto result = decoder.decode(encoded);
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
    const compressor::EncodedData encoded = encoder.encode(input);
    EXPECT_EQ(encoded.method_, compressor::Method::stored);
    EXPECT_EQ(encoded.size_, 512U);
    EXPECT_EQ(encoded.code_lengths_, compressor::CodeLengths{});
    EXPECT_EQ(encoded.payload_, input.data_);

    compressor::Engine decoder;
    EXPECT_EQ(decoder.decode(encoded), input);
}

TEST_F(EngineTests, Short_input_is_stored_because_the_huffman_header_costs_more)
{
    compressor::Engine encoder;
    const compressor::EncodedData encoded = encoder.encode(decoded(input));
    EXPECT_EQ(encoded.method_, compressor::Method::stored);

    compressor::Engine decoder;
    EXPECT_EQ(decoder.decode(encoded), decoded(input));
}

TEST_F(EngineTests, Empty_input_is_stored_and_round_trips)
{
    compressor::Engine encoder;
    const compressor::EncodedData encoded = encoder.encode(decoded(""));
    EXPECT_EQ(encoded.method_, compressor::Method::stored);
    EXPECT_EQ(encoded.size_, 0U);
    EXPECT_TRUE(encoded.payload_.empty());

    compressor::Engine decoder;
    EXPECT_EQ(decoder.decode(encoded), decoded(""));
}

TEST_F(EngineTests, A_tie_is_stored_and_one_byte_saved_is_huffman)
{
    // a = 0, b = 1. 147 bytes: 9 + 147 = 137 + 19. 148 bytes: 9 + 148 > 137 + 19.
    compressor::Engine engine;
    EXPECT_EQ(engine.encode(decoded(repeat("ab", 74).substr(0, 147))).method_,
              compressor::Method::stored);
    EXPECT_EQ(engine.encode(decoded(repeat("ab", 74))).method_, compressor::Method::huffman);
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
    compressor::EncodedData encoded;
    encoded.method_ = compressor::Method::stored;
    encoded.size_ = 3;

    compressor::Engine decoder;
    encoded.payload_ = {'a', 'b'};
    EXPECT_EQ(decoder.decode(encoded), std::unexpected(compressor::Error::corrupt_payload));
    encoded.payload_ = {'a', 'b', 'c', 'd'};
    EXPECT_EQ(decoder.decode(encoded), std::unexpected(compressor::Error::corrupt_payload));
}
