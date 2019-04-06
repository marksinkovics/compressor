#include <sstream>

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include <compressor/data/data.h>
#include <compressor/bitset/bitset.h>

class CompressorDataTests : public ::testing::Test {
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

TEST_F(CompressorDataTests, EncodedDataStream)
{
    compressor::EncodedData input_data;
    input_data.bit_dict_ = {
        { compressor::bitset("00"), 'g' },
        { compressor::bitset("001"), 'p' },
        { compressor::bitset("0011"), 's' },
        { compressor::bitset("0111"), 'r' },
        { compressor::bitset("10"), 'o' },
        { compressor::bitset("101"), ' ' },
        { compressor::bitset("1011"), 'e' },
        { compressor::bitset("1111"), 'h' },
    };
    input_data.data_ = compressor::bitset("01010");
    
    stream << input_data;
    
    compressor::EncodedData output_data;
    stream >> output_data;

    EXPECT_EQ(input_data.bit_dict_, output_data.bit_dict_);
    EXPECT_EQ(input_data.data_, output_data.data_);
}
