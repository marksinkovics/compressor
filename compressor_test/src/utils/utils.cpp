#include <gtest/gtest.h>

#include <compressor/utils/utils.h>

class UtilsTests : public ::testing::Test {};

TEST_F(UtilsTests, calc_num_blocks)
{
    constexpr std::size_t BITS_PER_BLOCK_8 = std::numeric_limits<uint8_t>::digits;
    EXPECT_EQ(compressor::utils::calc_num_blocks(8, BITS_PER_BLOCK_8), std::size_t(1));
    EXPECT_EQ(compressor::utils::calc_num_blocks(9, BITS_PER_BLOCK_8), std::size_t(2));
    
    constexpr std::size_t BITS_PER_BLOCK_16 = std::numeric_limits<uint16_t>::digits;
    EXPECT_EQ(compressor::utils::calc_num_blocks(16, BITS_PER_BLOCK_16), std::size_t(1));
    EXPECT_EQ(compressor::utils::calc_num_blocks(17, BITS_PER_BLOCK_16), std::size_t(2));

    constexpr std::size_t BITS_PER_BLOCK_64 = std::numeric_limits<uint64_t>::digits;
    EXPECT_EQ(compressor::utils::calc_num_blocks(64, BITS_PER_BLOCK_64), std::size_t(1));
    EXPECT_EQ(compressor::utils::calc_num_blocks(65, BITS_PER_BLOCK_64), std::size_t(2));
}

TEST_F(UtilsTests, block_to_bit_string)
{
    uint8_t bit8 = 1;
    ASSERT_EQ("00000001", compressor::utils::block_to_bit_string(bit8));
    ASSERT_EQ("01", compressor::utils::block_to_bit_string(bit8, uint8_t(2)));

    uint16_t bit16 = 1;
    ASSERT_EQ("0000000000000001", compressor::utils::block_to_bit_string(bit16));

    uint32_t bit32 = 1;
    ASSERT_EQ("00000000000000000000000000000001", compressor::utils::block_to_bit_string(bit32));

    uint64_t bit64 = 1;
    ASSERT_EQ("0000000000000000000000000000000000000000000000000000000000000001", compressor::utils::block_to_bit_string(bit64));
    ASSERT_EQ("01", compressor::utils::block_to_bit_string(bit64, uint64_t(2)));

}
