#include <gtest/gtest.h>

#include <compressor/utils/utils.h>

class UtilsTests : public ::testing::Test {};

TEST_F(UtilsTests, byte_to_bit_string)
{
    uint8_t byte = 1;
    ASSERT_EQ("00000001", Utils::byte_to_bit_string(byte));
    
    byte = 2;
    ASSERT_EQ("00000010", Utils::byte_to_bit_string(byte));
    ASSERT_EQ("10", Utils::byte_to_bit_string(byte, 2));
    ASSERT_EQ("010", Utils::byte_to_bit_string(byte, 3));
}
