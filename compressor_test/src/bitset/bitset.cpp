//
// https://jacksondunstan.com/articles/1946

#include <algorithm>
#include <chrono>
#include <iostream>
#include <sstream>

#include <boost/dynamic_bitset/dynamic_bitset.hpp>
#include <gtest/gtest.h>

#include <compressor/bitset/bitset.h>
#include <compressor/bitset/bitset_iterator.h>

#include <compressor/utils/utils.h>

class BitsetTests : public ::testing::Test {};

TEST_F(BitsetTests, Init) {
    compressor::bitset v1(1);
    ASSERT_EQ(v1.size(), 1);
    ASSERT_EQ(v1.num_bytes(), 1);
    
    compressor::bitset v2(0);
    ASSERT_EQ(v2.size(), 0);
    ASSERT_EQ(v2.num_bytes(), 0);
    
    compressor::bitset v3("010");
    ASSERT_EQ(v3.size(), 3);
    ASSERT_EQ(v3.num_bytes(), 1);
    ASSERT_FALSE(v3[0]);
    ASSERT_TRUE(v3[1]);
    ASSERT_FALSE(v3[2]);
    
    compressor::bitset v4("0101010101");
    ASSERT_EQ(v4.size(), 10);
    ASSERT_EQ(v4.num_bytes(), 2);
}

TEST_F(BitsetTests, Accessors) {
    compressor::bitset v1(8);
    v1.set(0, true);
    ASSERT_TRUE(v1[0]);
    v1.set(0, false);
    ASSERT_FALSE(v1[0]);
    v1.resize(10);
    v1.set(9);
    ASSERT_TRUE(v1.test(9));
    v1.reset(9);
    ASSERT_FALSE(v1.test(9));
}

TEST_F(BitsetTests, Accessors_Invalid) {
    compressor::bitset v1(8);
    testing::internal::CaptureStderr();
    v1.set(9, true);
    std::string error1 = ::testing::internal::GetCapturedStderr();
    ASSERT_STREQ(error1.c_str(), "Attempted to access an illegal position (9).\n");

    testing::internal::CaptureStderr();
    v1.set(-1, true);
    std::string error2 = ::testing::internal::GetCapturedStderr();
    std::stringstream expectedErrorMsgStream;
    expectedErrorMsgStream << "Attempted to access an illegal position (" <<  (size_t)-1  << ").\n";
    ASSERT_STREQ(error2.c_str(), expectedErrorMsgStream.str().c_str());
}

TEST_F(BitsetTests, ResizeInBits) {
    compressor::bitset v1(8);
    v1.set(0, true);
    v1.set(2, true);
    ASSERT_EQ(v1.size(), 8);
    ASSERT_EQ(v1.num_bytes(), 1);

    v1.resize(16);
    ASSERT_EQ(v1.size(), 16);
    ASSERT_EQ(v1.num_bytes(), 2);

    v1.resize(9);
    ASSERT_EQ(v1.size(), 9);
    ASSERT_EQ(v1.num_bytes(), 2);

    ASSERT_TRUE(v1[0]);
    ASSERT_TRUE(v1[2]);
}

TEST_F(BitsetTests, CopyOperators) {
    compressor::bitset v1(8);
    v1.set(0);
    v1.set(7);

    compressor::bitset v2(v1);
    ASSERT_TRUE(v2.test(0));
    ASSERT_TRUE(v2.test(7));

    v1.resize(16);
    
    ASSERT_EQ(8, v2.size());
    
    v1.set(10);
    compressor::bitset v3(v1);
    ASSERT_TRUE(v3.test(0));
    ASSERT_TRUE(v3.test(7));
    ASSERT_TRUE(v3.test(10));
    
    compressor::bitset v4 = v1;
    ASSERT_EQ(16, v4.size());
    ASSERT_TRUE(v4.test(0));
    ASSERT_TRUE(v4.test(7));
}

TEST_F(BitsetTests, MoveOperators) {
    compressor::bitset v1(9);
    v1.set(0);
    ASSERT_TRUE(v1.test(0));
    v1.set(5);
    ASSERT_TRUE(v1.test(5));
    
    // Move constructor
    compressor::bitset v2(std::move(v1));

    ASSERT_EQ(0, v1.size());

    ASSERT_EQ(9, v2.size());
    ASSERT_TRUE(v2.test(0));
    ASSERT_TRUE(v2.test(5));
   
    // Move assignment
    compressor::bitset v3(1);
    v3 = std::move(v2);
    ASSERT_EQ(9, v3.size());
    ASSERT_TRUE(v3.test(0));
    ASSERT_TRUE(v3.test(5));
}

TEST_F(BitsetTests, AssignmentOperator) {
    compressor::bitset v1(8);
    v1.set(0);
    v1.set(7);

    compressor::bitset v2 = v1;
    ASSERT_TRUE(v2.test(0));
    ASSERT_TRUE(v2.test(7));

    v1.resize(16);
    v1.set(10);

    compressor::bitset v3 = v1;
    ASSERT_TRUE(v3.test(0));
    ASSERT_TRUE(v3.test(7));
    ASSERT_TRUE(v3.test(10));
}

//Relational operators
TEST_F(BitsetTests, Relational)
{
    compressor::bitset v1{"0101"};
    compressor::bitset v2{"0101"};
    compressor::bitset v3{"1101"};
    compressor::bitset v4{"10101"};
    
    ASSERT_EQ(v1, v2); // 0101 == 0101
    ASSERT_NE(v1, v3); // 0101 != 1101
    ASSERT_GE(v3, v1); // 1101 >  0101
    ASSERT_LE(v1, v3); // 0101 <  1101
    ASSERT_LE(v1, v4); // 0101 <  10101
}

TEST_F(BitsetTests, ToString) {
    
    compressor::bitset v1(16);
    v1.set(2);
    v1.set(4);
    v1.set(15);
    std::stringstream stream;
    stream << v1.str();
    ASSERT_EQ("1000000000010100", stream.str());
    
    compressor::bitset v2(11);
    v2.set(2);
    v2.set(4);
    v2.set(10);
    std::stringstream stream2;
    stream2 << v2.str();
    ASSERT_EQ("10000010100", stream2.str());
}

TEST_F(BitsetTests, bitsets)
{
    compressor::bitset v1(11);
    v1.set(2);
    v1.set(4);
    v1.set(10);
    std::stringstream stream1;
    stream1 << v1.str();
    
    std::bitset<11> b1;
    b1.set(2);
    b1.set(4);
    b1.set(10);
    std::stringstream stream2;
    stream2 << b1;
    
    ASSERT_EQ(stream1.str(), stream2.str());
    
    boost::dynamic_bitset<> db1(11);
    db1[2] = 1;
    db1[4] = 1;
    db1[10] = 1;
    std::stringstream stream3;
    stream3 << db1;
    
    ASSERT_EQ(stream1.str(), stream3.str());
}

TEST_F(BitsetTests, push_back)
{
    compressor::bitset v1(0);        //
    v1.push_back(true);     //1
    ASSERT_TRUE(v1[0]);
    v1.push_back(false);    //01
    ASSERT_FALSE(v1[1]);
    v1.push_back(true);     //101
    ASSERT_TRUE(v1[2]);
    v1.push_back(true);     //1101
    ASSERT_TRUE(v1[3]);
}

TEST_F(BitsetTests, Append)
{
    compressor::bitset v1("011");
    compressor::bitset v2("00");    //00
    v2.append(v1);      //01100
    EXPECT_EQ(compressor::bitset("01100"), v2);
    v2.append(v1);
    EXPECT_EQ(compressor::bitset("01101100"), v2);
}

TEST_F(BitsetTests, File)
{
    std::stringstream stream;
    compressor::bitset in0;
    compressor::bitset in1("0");
    compressor::bitset in2("101101101");
    compressor::bitset in3("101101101101101101");
    
    uint8_t in4 = 42;
    
    std::cout << "in0  " << in0.str() << " Bit size: " << in0.size() << '\n';
    std::cout << "in1  " << in1.str() << " Bit size: " << in1.size() << '\n';
    std::cout << "in2  " << in2.str() << " Bit size: " << in2.size() << '\n';
    std::cout << "in3  " << in3.str() << " Bit size: " << in3.size() << '\n';
    std::cout << "in4  " << +in4 << '\n';

    stream << in0;
    stream << in1;
    stream << in2;
    stream << in3;
    stream << in4;
    
    compressor::bitset out0, out1, out2, out3;
    int8_t out4;
    
    stream >> out0;
    stream >> out1;
    stream >> out2;
    stream >> out3;
    stream >> out4;

    std::cout << "out0 " << out0.str() << " Bit size: " << out0.size() << '\n';
    std::cout << "out1 " << out1.str() << " Bit size: " << out1.size() << '\n';
    std::cout << "out2 " << out2.str() << " Bit size: " << out2.size() << '\n';
    std::cout << "out3 " << out3.str() << " Bit size: " << out3.size() << '\n';
    std::cout << "out4 " << +out4 << '\n';

    ASSERT_EQ(in0, out0);
    ASSERT_EQ(in1, out1);
    ASSERT_EQ(in2, out2);
    ASSERT_EQ(in3, out3);
    ASSERT_EQ(in4, out4);
}

TEST_F(BitsetTests, Copying)
{
    compressor::bitset v1("101");
    compressor::bitset v2;
    std::copy(v1.begin(), v1.end(), std::back_inserter(v2));
    EXPECT_EQ(v1, v2);
}
