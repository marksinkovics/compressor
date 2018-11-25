//
// https://jacksondunstan.com/articles/1946

#include <chrono>
#include <iostream>
#include <sstream>

#include <boost/dynamic_bitset/dynamic_bitset.hpp>
#include <gtest/gtest.h>

#include <bitvector/BitVector.hpp>
#include <bitvector/BitVectorIterator.hpp>

class BitVectorTests : public ::testing::Test {};

TEST_F(BitVectorTests, Init) {
    BitVector v1;
    ASSERT_EQ(v1.size_in_bits(), 8);
    ASSERT_EQ(v1.size_in_bytes(), 1);
}

TEST_F(BitVectorTests, Accessors) {
    BitVector v1;
    v1.set(0, true);
    ASSERT_TRUE(v1.get(0));
    ASSERT_TRUE(v1[0]);
    v1.set(0, false);
    ASSERT_FALSE(v1.get(0));
    ASSERT_FALSE(v1[0]);
    v1.resize_in_bits(10);
    v1.set(9);
    ASSERT_TRUE(v1.test(9));
    v1.reset(9);
    ASSERT_FALSE(v1.test(9));
}

TEST_F(BitVectorTests, Accessors_Invalid) {
    BitVector v1;
    testing::internal::CaptureStderr();
    v1.set(9, true);
    std::string error1 = ::testing::internal::GetCapturedStderr();
    ASSERT_STREQ(error1.c_str(), "Attempted to access an illegal position.\n");

    testing::internal::CaptureStderr();
    v1.set(-1, true);
    std::string error2 = ::testing::internal::GetCapturedStderr();
    ASSERT_STREQ(error2.c_str(), "Attempted to access an illegal position.\n");

}

TEST_F(BitVectorTests, ResizeInBytes) {
    BitVector v1;
    v1.set(0, true);
    v1.set(2, true);
    ASSERT_EQ(v1.size_in_bits(), 8);
    ASSERT_EQ(v1.size_in_bytes(), 1);
    v1.resize_in_bytes(2);
    ASSERT_EQ(v1.size_in_bits(), 16);
    ASSERT_EQ(v1.size_in_bytes(), 2);
    ASSERT_TRUE(v1.get(0));
    ASSERT_TRUE(v1.get(2));
}

TEST_F(BitVectorTests, ResizeInBits) {
    BitVector v1;
    v1.set(0, true);
    v1.set(2, true);
    ASSERT_EQ(v1.size_in_bits(), 8);
    ASSERT_EQ(v1.size_in_bytes(), 1);

    v1.resize_in_bits(2);
    ASSERT_EQ(v1.size_in_bits(), 8);
    ASSERT_EQ(v1.size_in_bytes(), 1);

    v1.resize_in_bits(9);
    ASSERT_EQ(v1.size_in_bits(), 16);
    ASSERT_EQ(v1.size_in_bytes(), 2);

    ASSERT_TRUE(v1.get(0));
    ASSERT_TRUE(v1.get(2));
}

TEST_F(BitVectorTests, CopyConstructor) {
    BitVector v1;
    v1.set(0);
    v1.set(7);

    BitVector v2(v1);
    ASSERT_TRUE(v2.test(0));
    ASSERT_TRUE(v2.test(7));

    v1.resize_in_bytes(2);
    v1.set(10);

    BitVector v3(v1);
    ASSERT_TRUE(v3.test(0));
    ASSERT_TRUE(v3.test(7));
    ASSERT_TRUE(v3.test(10));
}

TEST_F(BitVectorTests, AssignmentOperator) {
    BitVector v1;
    v1.set(0);
    v1.set(7);

    BitVector v2 = v1;
    ASSERT_TRUE(v2.test(0));
    ASSERT_TRUE(v2.test(7));

    v1.resize_in_bytes(2);
    v1.set(10);

    BitVector v3 = v1;
    ASSERT_TRUE(v3.test(0));
    ASSERT_TRUE(v3.test(7));
    ASSERT_TRUE(v3.test(10));
}

TEST_F(BitVectorTests, ToString) {
    
    BitVector v1(2);
    v1.set(2);
    v1.set(4);
    v1.set(15);
    std::stringstream stream;
    stream << v1;
    ASSERT_EQ("1000000000010100", stream.str());
    
    BitVector v2;
    v2.resize_in_bits(11);
    v2.set(2);
    v2.set(4);
    v2.set(10);
    std::stringstream stream2;
    stream2 << v2;
    ASSERT_EQ("10000010100", stream2.str());
}

TEST_F(BitVectorTests, std_bitset)
{
    BitVector v1;
    v1.resize_in_bits(11);
    v1.set(2);
    v1.set(4);
    v1.set(10);
    std::stringstream stream1;
    stream1 << v1;
    
    std::bitset<11> b1;
    b1.set(2);
    b1.set(4);
    b1.set(10);
    std::stringstream stream2;
    stream2 << b1;
    
    ASSERT_EQ(stream1.str(), stream2.str());
}

TEST_F(BitVectorTests, boost_dynamic_bitset)
{
    BitVector v1;
    v1.resize_in_bits(11);
    v1.set(2);
    v1.set(4);
    v1.set(10);
    std::stringstream stream1;
    stream1 << v1;
    
    boost::dynamic_bitset<> db1(11);
    db1[2] = 1;
    db1[4] = 1;
    db1[10] = 1;
    std::stringstream stream2;
    stream2 << db1;
    
    ASSERT_EQ(stream1.str(), stream2.str());

}

//TEST_F(BitVectorTests, byte_to_bit_string) {
//
//    uint8_t byte = 1;
//    ASSERT_EQ("00000001", Utils::byte_to_bit_string(byte));
//
//    byte = 2;
//    ASSERT_EQ("00000010", Utils::byte_to_bit_string(byte));
//    ASSERT_EQ("10", Utils::byte_to_bit_string(byte, 2));
//    ASSERT_EQ("010", Utils::byte_to_bit_string(byte, 3));
//}
