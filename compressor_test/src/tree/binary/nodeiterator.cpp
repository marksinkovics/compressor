#include <iostream>
#include <memory>


#include <sstream>

#include <gtest/gtest.h>

#include <compressor/tree/binary/node.h>
#include <compressor/tree/binary/nodeiterator.h>
#include <compressor/tree/binary/tree.h>
#include <compressor/tree/symbolnode.h>

class BinaryNodeIteratorTests : public ::testing::Test {
protected:
    
    std::shared_ptr<compressor::BinaryTree> _tree;
    
    std::shared_ptr<compressor::SymbolNode> node1 = std::make_shared<compressor::SymbolNode>("1");
    std::shared_ptr<compressor::SymbolNode> node2 = std::make_shared<compressor::SymbolNode>("2");
    std::shared_ptr<compressor::SymbolNode> node3 = std::make_shared<compressor::SymbolNode>("3");
    std::shared_ptr<compressor::SymbolNode> node4 = std::make_shared<compressor::SymbolNode>("4");
    std::shared_ptr<compressor::SymbolNode> node5 = std::make_shared<compressor::SymbolNode>("5");
    std::shared_ptr<compressor::SymbolNode> node6 = std::make_shared<compressor::SymbolNode>("6");
    std::shared_ptr<compressor::SymbolNode> node7 = std::make_shared<compressor::SymbolNode>("7");
    std::shared_ptr<compressor::SymbolNode> node8 = std::make_shared<compressor::SymbolNode>("8");
    std::shared_ptr<compressor::SymbolNode> node9 = std::make_shared<compressor::SymbolNode>("9");
    
    void setLeft(std::shared_ptr<compressor::SymbolNode>& node, std::shared_ptr<compressor::SymbolNode>& left)
    {
        node->setLeft(left);
        left->setParent(node);
    }

    void setRight(std::shared_ptr<compressor::SymbolNode>& node, std::shared_ptr<compressor::SymbolNode>& right)
    {
        node->setRight(right);
        right->setParent(node);
    }
    
    std::shared_ptr<compressor::SymbolNode> buildTree() {
        setLeft(node1, node2);
        setRight(node1, node3);
        
        setLeft(node2, node4);
        setRight(node2, node5);
        
        setLeft(node3, node6);
        setRight(node3, node7);
        
        setLeft(node6, node8);
        setRight(node6, node9);
        
        return node1;
    }
    
    void SetUp() override
    {
        auto node = buildTree();
        _tree = std::make_shared<compressor::BinaryTree>();
        _tree->setRoot(node);
    }
};

TEST_F(BinaryNodeIteratorTests, Equality)
{
    auto iterBegin = _tree->preOrderBegin();
    auto iterEnd = _tree->preOrderEnd();

    EXPECT_EQ(iterBegin, iterBegin);
    EXPECT_EQ(iterEnd, iterEnd);
    EXPECT_NE(iterBegin, iterEnd);
}

TEST_F(BinaryNodeIteratorTests, Relatives)
{
    EXPECT_EQ(_tree->root(), node1);
    EXPECT_EQ(node1->left(), node2);
    EXPECT_EQ(node1->right(), node3);
    EXPECT_EQ(node2->left(), node4);
    EXPECT_EQ(node2->right(), node5);
    
    EXPECT_EQ(node4->left(), nullptr);
    EXPECT_EQ(node4->right(), nullptr);
    EXPECT_EQ(node5->left(), nullptr);
    EXPECT_EQ(node5->right(), nullptr);
    
    EXPECT_EQ(node3->left(), node6);
    EXPECT_EQ(node3->right(), node7);

    EXPECT_EQ(node6->left(), node8);
    EXPECT_EQ(node6->right(), node9);
    EXPECT_EQ(node8->left(), nullptr);
    EXPECT_EQ(node8->right(), nullptr);
    
    EXPECT_EQ(node9->left(), nullptr);
    EXPECT_EQ(node9->right(), nullptr);

    EXPECT_EQ(node7->left(), nullptr);
    EXPECT_EQ(node7->right(), nullptr);
    
}

TEST_F(BinaryNodeIteratorTests, ForIterator)
{
    auto begin = _tree->preOrderBegin();
    auto end = _tree->preOrderEnd();
    std::stringstream ss;
    for(auto iter = begin; iter != end; iter++)
    {
        auto node = std::dynamic_pointer_cast<compressor::SymbolNode>(*iter);
        ss << node->value();
    }
    EXPECT_STREQ(ss.str().c_str(), "124536897");
    std::cout << '\n';
}

TEST_F(BinaryNodeIteratorTests, ForEachIterator)
{
    auto begin = _tree->preOrderBegin();
    auto end = _tree->preOrderEnd();
    std::stringstream ss;
    std::for_each(begin, end, [&ss](const auto& bin_node){
        auto node = std::dynamic_pointer_cast<compressor::SymbolNode>(bin_node);
        ss << node->value();
    });
    EXPECT_STREQ(ss.str().c_str(), "124536897");
    std::cout << '\n';
}

TEST_F(BinaryNodeIteratorTests, Tagging)
{
    std::for_each(_tree->preOrderBegin(), _tree->preOrderEnd(), [](const auto &bin_node) {
        std::shared_ptr<compressor::SymbolNode> node = std::dynamic_pointer_cast<compressor::SymbolNode>(bin_node);
    });
}

