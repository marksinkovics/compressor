#include <gtest/gtest.h>

#include <bitvector/bitvector.h>
#include <bitvector/bitvectoriterator.h>

class BitVectorIteratorTests : public ::testing::Test {
public:
    BitVector *v;
protected:
    void SetUp() override {
        v = new BitVector(16);
        v->set(0);
        v->set(7);
        v->set(10);
        v->set(15);
        // 10000100 10000001
    }

    void TearDown() override {
        delete v;
    }
};

TEST_F(BitVectorIteratorTests, Iterator) {
    BitVectorIterator it = v->begin();
    //    BitVectorIterator it_end = v.end();
    ASSERT_TRUE(*it);       // 0
    ASSERT_FALSE(*(++it));  // 1
    ASSERT_TRUE(*(--it));   // 0
    ASSERT_TRUE(*(it++));   // -> 1
    ASSERT_FALSE(*(it--));  // -> 0
    it += 7;                // 7
    ASSERT_TRUE(*it);
    it -= 6;                // 1
    ASSERT_FALSE(*it);

    it = it + 9;            // 10
    ASSERT_TRUE(*it);

    it = it - 3;            // 7
    ASSERT_TRUE(*it);

    ASSERT_EQ(it, it);
    ASSERT_NE(it, it++);    // 8
}

TEST_F(BitVectorIteratorTests, Increment) {
    BitVectorIterator it = v->begin();
    ASSERT_TRUE(*it);       // 0
    ASSERT_FALSE(*(++it));  // 1
    ASSERT_FALSE(*(it++));  // 1
}

TEST_F(BitVectorIteratorTests, Decrement) {
    BitVectorIterator it = v->end();
    ASSERT_FALSE(*it); // it always must be false
    ASSERT_TRUE(*(--it));
    ASSERT_FALSE(*(--it));  // 14
    ASSERT_FALSE(*(it++));  // 15
}

TEST_F(BitVectorIteratorTests, ArithmeticOperators) {
    BitVectorIterator it = v->begin();
    it += 1;
    ASSERT_FALSE(*it);
    it += 6;
    ASSERT_TRUE(*it);
    it -= 6;
    ASSERT_FALSE(*it);
    it -= 1;
    ASSERT_TRUE(*it);
    it = it + 15;
    ASSERT_TRUE(*it);
    it = it -14;
    ASSERT_FALSE(*it);
}

TEST_F(BitVectorIteratorTests, RelationalOperators) {
    BitVectorIterator it = v->begin();
    ASSERT_EQ(it, it);
    ASSERT_NE(it, it++);

    it = v->end();
    ASSERT_EQ(it, it);
    ASSERT_NE(it, it--);
}
