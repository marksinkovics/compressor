#include <sstream>

#include <gtest/gtest.h>

#include <compressor/compressor.h>
#include <compressor/data/data.h>

class CompressorTests : public ::testing::Test {};

TEST_F(CompressorTests, Compression)
{
    const std::string input = "go go gophers";
    compressor::Compressor comp1(input);
    compressor::EncodedData encoded_data = comp1.encode();
    
    compressor::Compressor comp2(encoded_data);
    compressor::Data decoded_data = comp2.decode();

    std::string decoded_str(decoded_data.data_.begin(), decoded_data.data_.end());
    
    EXPECT_STREQ(input.c_str(), decoded_str.c_str());
}

TEST_F(CompressorTests, Stream)
{
    std::stringstream stream;
    const std::string input = "go go gophers";
    compressor::Compressor comp1(input);
    compressor::EncodedData encoded_data = comp1.encode();
    
    stream << encoded_data;
    
    compressor::EncodedData encoded_data2;
    stream >> encoded_data2;
    
    compressor::Compressor comp2(encoded_data2);
    compressor::Data decoded_data = comp2.decode();
    
    std::string decoded_str(decoded_data.data_.begin(), decoded_data.data_.end());
    
    EXPECT_STREQ(input.c_str(), decoded_str.c_str());
}
