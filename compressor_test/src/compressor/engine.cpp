#include <sstream>

#include <gtest/gtest.h>

#include <compressor/engine.h>
#include <compressor/data/data.h>

class EngineTests : public ::testing::Test {};

TEST_F(EngineTests, Compression)
{
    const std::string input = "go go gophers";
    compressor::Engine comp1(input);
    compressor::EncodedData encoded_data = comp1.encode();
    
    compressor::Engine comp2(encoded_data);
    compressor::Data decoded_data = comp2.decode();

    std::string decoded_str(decoded_data.data_.begin(), decoded_data.data_.end());
    
    EXPECT_STREQ(input.c_str(), decoded_str.c_str());
}

TEST_F(EngineTests, Stream)
{
    std::stringstream stream;
    const std::string input = "go go gophers";
    compressor::Engine comp1(input);
    compressor::EncodedData encoded_data = comp1.encode();
    
    stream << encoded_data;
    
    compressor::EncodedData encoded_data2;
    stream >> encoded_data2;
    
    compressor::Engine comp2(encoded_data2);
    compressor::Data decoded_data = comp2.decode();
    
    std::string decoded_str(decoded_data.data_.begin(), decoded_data.data_.end());
    
    EXPECT_STREQ(input.c_str(), decoded_str.c_str());
}
