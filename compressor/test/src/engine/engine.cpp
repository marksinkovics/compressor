#include <algorithm>
#include <cstdint>
#include <sstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <compressor/data/data.h>
#include <compressor/engine/engine.h>

class EngineTests : public ::testing::Test
{
protected:
    const std::string input = "go go gophers";
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
    compressor::EncodedData encoded_data = comp1.encode(compressor::DecodedData(input));

    compressor::Engine comp2;
    compressor::DecodedData decoded_data = comp2.decode(encoded_data);

    std::string decoded_str(decoded_data.data_.begin(), decoded_data.data_.end());

    EXPECT_STREQ(input.c_str(), decoded_str.c_str());
}

TEST_F(EngineTests, Compression_with_long_text)
{
    compressor::Engine comp1;
    compressor::EncodedData encoded_data = comp1.encode(compressor::DecodedData(longInput));

    compressor::Engine comp2;
    compressor::DecodedData decoded_data = comp2.decode(encoded_data);

    std::string decoded_str(decoded_data.data_.begin(), decoded_data.data_.end());

    EXPECT_STREQ(longInput.c_str(), decoded_str.c_str());
}

TEST_F(EngineTests, Stream)
{
    std::stringstream stream;
    compressor::Engine comp1;
    compressor::EncodedData encoded_data = comp1.encode(compressor::DecodedData(input));

    stream << encoded_data;

    compressor::EncodedData encoded_data2;
    stream >> encoded_data2;

    compressor::Engine comp2;
    compressor::DecodedData decoded_data = comp2.decode(encoded_data2);

    std::string decoded_str(decoded_data.data_.begin(), decoded_data.data_.end());

    EXPECT_STREQ(input.c_str(), decoded_str.c_str());
}

TEST_F(EngineTests, Encoding_twice_gives_the_same_result_as_a_fresh_engine)
{
    compressor::Engine reused;
    reused.encode(compressor::DecodedData(longInput));
    compressor::EncodedData second = reused.encode(compressor::DecodedData(input));

    compressor::Engine fresh;
    EXPECT_EQ(second, fresh.encode(compressor::DecodedData(input)));
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
    const compressor::EncodedData encoded = encoder.encode(compressor::DecodedData(skewed));
    EXPECT_EQ(*std::ranges::max_element(encoded.code_lengths_), compressor::max_code_length);

    std::stringstream stream;
    stream << encoded;
    compressor::EncodedData read_back;
    stream >> read_back;

    EXPECT_EQ(read_back, encoded);

    compressor::Engine decoder;
    const compressor::DecodedData decoded = decoder.decode(read_back);
    EXPECT_EQ(std::string(decoded.data_.begin(), decoded.data_.end()), skewed);
}

TEST_F(EngineTests, Encode_packs_codes_first_bit_most_significant)
{
    // a and b both get length 1: a = 0, b = 1.
    compressor::Engine engine;
    EXPECT_EQ(engine.encode(compressor::DecodedData("aab")).payload_, std::vector<uint8_t>{0x20});
}

TEST_F(EngineTests, Encode_packs_codes_across_bytes_and_pads_the_last_one)
{
    // a = 0, b = 10, c = 11, so "abac" is 010011; six of them are 36 bits, past one 32-bit write.
    std::string input;
    for (int i = 0; i < 6; ++i)
    {
        input += "abac";
    }
    compressor::Engine engine;
    EXPECT_EQ(engine.encode(compressor::DecodedData(input)).payload_,
              (std::vector<uint8_t>{0x4D, 0x34, 0xD3, 0x4D, 0x30}));
}

TEST_F(EngineTests, A_truncated_payload_decodes_to_nothing)
{
    compressor::Engine encoder;
    compressor::EncodedData encoded = encoder.encode(compressor::DecodedData(longInput));
    encoded.payload_.resize(encoded.payload_.size() / 2);

    compressor::Engine decoder;
    EXPECT_TRUE(decoder.decode(encoded).data_.empty());
}

TEST_F(EngineTests, Print_dict_lists_the_canonical_codes)
{
    compressor::Engine engine;
    engine.encode(compressor::DecodedData("aab"));

    testing::internal::CaptureStdout();
    engine.print_dict();
    EXPECT_EQ(testing::internal::GetCapturedStdout(), "0 => [a]\n1 => [b]\n");
}

TEST_F(EngineTests, Print_graph_rebuilds_the_tree_from_the_codes)
{
    compressor::Engine engine;
    engine.encode(compressor::DecodedData("aab"));

    testing::internal::CaptureStdout();
    engine.print_graph();
    EXPECT_EQ(testing::internal::GetCapturedStdout(), "digraph G {\n"
                                                      "\t\"n\" [label=\"ab [3, ]\"];\n"
                                                      "\t\"n\" -> \"n0\";\n"
                                                      "\t\"n\" -> \"n1\";\n"
                                                      "\t\"n0\" [label=\"a [2, 0]\"];\n"
                                                      "\t\"n1\" [label=\"b [1, 1]\"];\n"
                                                      "}\n");
}

TEST_F(EngineTests, Print_graph_handles_a_single_symbol)
{
    compressor::Engine engine;
    engine.encode(compressor::DecodedData("aaaa"));

    testing::internal::CaptureStdout();
    engine.print_graph();
    EXPECT_EQ(testing::internal::GetCapturedStdout(), "digraph G {\n"
                                                      "\t\"n\" [label=\"a [4, ]\"];\n"
                                                      "\t\"n\" -> \"n0\";\n"
                                                      "\t\"n0\" [label=\"a [4, 0]\"];\n"
                                                      "}\n");
}

TEST_F(EngineTests, A_bit_pattern_that_starts_no_code_decodes_to_nothing)
{
    // A single symbol has the code 0, so no code starts with 1.
    compressor::EncodedData encoded;
    encoded.size_ = 2;
    encoded.code_lengths_['a'] = 1;
    encoded.payload_ = {0x40}; // 01

    compressor::Engine decoder;
    EXPECT_TRUE(decoder.decode(encoded).data_.empty());
}

TEST_F(EngineTests, A_code_that_ends_past_the_payload_decodes_to_nothing)
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
    EXPECT_TRUE(decoder.decode(encoded).data_.empty());
}
