//
//  BitVectorTests.cpp
//  HuffmanTest
//
//  Created by Mark Sinkovics on 2018. 06. 16..
//  Copyright © 2018. Mark Sinkovics. All rights reserved.
//
// https://jacksondunstan.com/articles/1946

#include <chrono>
#include <iostream>

#include <boost/dynamic_bitset.hpp>
#include <gtest/gtest.h>

#include <BitVector.hpp>

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
    std::string error = ::testing::internal::GetCapturedStderr();
    ASSERT_STREQ(error.c_str(), "Attempted to access an illegal position.\n");
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

TEST_F(BitVectorTests, Iterator) {
    BitVector v1(2);
    v1.set(0);
    v1.set(7);
    v1.set(10);
    
    std::bitset<16> b;
    b.set(0);
    b.set(7);
    b.set(10);


//    BitVectorIterator it = v1.begin();
//    BitVectorIterator it_end = v1.end();
//
//    // Test the equality / inequality.
//    std::cout << "it == it: " << (it == it) << "\n";
//    std::cout << "it != it: " << (it != it) << "\n";
//
//    // Test dereferencing and iteration.
//    std::cout << "First value: " << *it << "\n";
//    std::cout << "Second value: " << *(++it) << "\n";
//    std::cout << "Previous (first) value: " << *(--it) << "\n";
//
//    // Test range for loop.
//    for (const auto n : v1)
//        std::cout << n;
//    std::cout << '\n';
//    std::cout << v1 << std::endl;
    
    
    
    // Test standard library algorithm.
    std::for_each(v1.begin(), v1.end(),
                  [](const auto& n) { std::cout << n;});
    std::cout << '\n';
    std::cout << v1 << std::endl;
    
//    std::for_each(b.begin(), b.end(),
//                  [](const auto& n) { std::cout << n;});
    std::cout << '\n';
    std::cout << b << std::endl;
}

TEST_F(BitVectorTests, ToString) {
    BitVector v1(2);
    v1.set(2);
    v1.set(4);
    std::cout << "BitVector:\n" << v1 << '\n';

    std::bitset<16> b1;
    b1.set(2);
    b1.set(4);
    std::cout << "std::bitset:\n" << b1 << '\n';

    boost::dynamic_bitset<> db1(16);
    db1[2] = 1;
    db1[4] = 1;
    std::cout << "boost::dynamic_bitset:\n" << db1 << '\n';
}
