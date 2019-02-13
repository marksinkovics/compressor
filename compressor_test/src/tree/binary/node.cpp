#include <memory>

#include <gtest/gtest.h>

#include <compressor/tree/binary/node.h>

class BinaryNodeTests : public ::testing::Test {};

TEST_F(BinaryNodeTests, Alloc)
{
    compressor::BinaryNode node;
    EXPECT_EQ(node.left(), nullptr);
    EXPECT_EQ(node.right(), nullptr);
    EXPECT_FALSE(node.hasParent());
}

TEST_F(BinaryNodeTests, MultipleAllocation)
{
    auto left = std::make_shared<compressor::BinaryNode>();
    auto right = std::make_shared<compressor::BinaryNode>();
    auto parent = std::make_shared<compressor::BinaryNode>();
    parent->setLeft(left);
    left->setParent(parent);
    parent->setRight(right);
    right->setParent(parent);
    
    EXPECT_EQ(parent->left(), left);
    EXPECT_EQ(parent->right(), right);
    
    EXPECT_TRUE(left->hasParent());
    EXPECT_TRUE(right->hasParent());
}
