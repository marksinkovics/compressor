#include <sstream>

#include <gtest/gtest.h>

#include <compressor/engine/engine.h>
#include <compressor/data/data.h>

class EngineTests : public ::testing::Test {
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
