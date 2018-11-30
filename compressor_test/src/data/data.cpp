#include <sstream>

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include <compressor/data/data.h>

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
    Compressor::Data input_data;
    input_data.data_ = {1, 2, 3, 4, 5};
    stream << input_data;
    
    Compressor::Data output_data;
    stream >> output_data;
    
    EXPECT_EQ(input_data.data_, output_data.data_);
    ASSERT_THAT(output_data.data_, testing::ElementsAre(1, 2, 3, 4, 5));
}

TEST_F(CompressorDataTests, EncodedDataStream)
{
    Compressor::EncodedData input_data;
    input_data.bit_dict_ = {
        { BitVector("00"), 'g' },
        { BitVector("001"), 'p' },
        { BitVector("0011"), 's' },
        { BitVector("0111"), 'r' },
        { BitVector("10"), 'o' },
        { BitVector("101"), ' ' },
        { BitVector("1011"), 'e' },
        { BitVector("1111"), 'h' },
    };
    input_data.data_ = BitVector("01010");
    
    stream << input_data;
    
    Compressor::EncodedData output_data;
    stream >> output_data;

    EXPECT_EQ(input_data.bit_dict_, output_data.bit_dict_);
    EXPECT_EQ(input_data.data_, output_data.data_);
}
