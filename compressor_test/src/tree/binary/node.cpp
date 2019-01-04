#include <memory>

#include <gtest/gtest.h>

#include <compressor/tree/binary/node.h>

class BinaryNodeTests : public ::testing::Test {};

TEST_F(BinaryNodeTests, Alloc)
{
    compressor::BinaryNode node;
    ASSERT_EQ(node.left(), nullptr);
    ASSERT_EQ(node.right(), nullptr);
    ASSERT_FALSE(node.hasParent());
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
    
    ASSERT_EQ(parent->left(), left);
    ASSERT_EQ(parent->right(), right);
    
    ASSERT_TRUE(left->hasParent());
    ASSERT_TRUE(right->hasParent());
}
