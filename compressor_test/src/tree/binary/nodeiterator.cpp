#include <iostream>
#include <memory>


#include <sstream>

#include <gtest/gtest.h>

#include <tree/binary/node.h>
#include <tree/binary/nodeiterator.h>
#include <tree/binary/tree.h>
#include <tree/symbolnode.h>

class BinaryNodeIteratorTests : public ::testing::Test {
protected:
    
    std::shared_ptr<BinaryTree> _tree;
    
    std::shared_ptr<SymbolNode> node1 = std::make_shared<SymbolNode>("1");
    std::shared_ptr<SymbolNode> node2 = std::make_shared<SymbolNode>("2");
    std::shared_ptr<SymbolNode> node3 = std::make_shared<SymbolNode>("3");
    std::shared_ptr<SymbolNode> node4 = std::make_shared<SymbolNode>("4");
    std::shared_ptr<SymbolNode> node5 = std::make_shared<SymbolNode>("5");
    std::shared_ptr<SymbolNode> node6 = std::make_shared<SymbolNode>("6");
    std::shared_ptr<SymbolNode> node7 = std::make_shared<SymbolNode>("7");
    std::shared_ptr<SymbolNode> node8 = std::make_shared<SymbolNode>("8");
    std::shared_ptr<SymbolNode> node9 = std::make_shared<SymbolNode>("9");
    
    void setLeft(std::shared_ptr<SymbolNode>& node, std::shared_ptr<SymbolNode>& left)
    {
        node->setLeft(left);
        left->setParent(node);
    }

    void setRight(std::shared_ptr<SymbolNode>& node, std::shared_ptr<SymbolNode>& right)
    {
        node->setRight(right);
        right->setParent(node);
    }
    
    std::shared_ptr<SymbolNode> buildTree() {
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
        _tree = std::make_shared<BinaryTree>();
        _tree->setRoot(node);
    }
};

TEST_F(BinaryNodeIteratorTests, Equality)
{
    auto iterBegin = _tree->preOrderBegin();
    auto iterEnd = _tree->preOrderEnd();

    ASSERT_EQ(iterBegin, iterBegin);
    ASSERT_EQ(iterEnd, iterEnd);
    ASSERT_NE(iterBegin, iterEnd);
}

TEST_F(BinaryNodeIteratorTests, Relatives)
{
    ASSERT_EQ(_tree->root(), node1);
    ASSERT_EQ(node1->left(), node2);
    ASSERT_EQ(node1->right(), node3);
    ASSERT_EQ(node2->left(), node4);
    ASSERT_EQ(node2->right(), node5);
    
    ASSERT_EQ(node4->left(), nullptr);
    ASSERT_EQ(node4->right(), nullptr);
    ASSERT_EQ(node5->left(), nullptr);
    ASSERT_EQ(node5->right(), nullptr);
    
    ASSERT_EQ(node3->left(), node6);
    ASSERT_EQ(node3->right(), node7);

    ASSERT_EQ(node6->left(), node8);
    ASSERT_EQ(node6->right(), node9);
    ASSERT_EQ(node8->left(), nullptr);
    ASSERT_EQ(node8->right(), nullptr);
    
    ASSERT_EQ(node9->left(), nullptr);
    ASSERT_EQ(node9->right(), nullptr);

    ASSERT_EQ(node7->left(), nullptr);
    ASSERT_EQ(node7->right(), nullptr);
    
}

TEST_F(BinaryNodeIteratorTests, ForIterator)
{
    auto begin = _tree->preOrderBegin();
    auto end = _tree->preOrderEnd();
    std::stringstream ss;
    for(auto iter = begin; iter != end; iter++)
    {
        auto node = std::dynamic_pointer_cast<SymbolNode>(*iter);
        ss << node->value();
    }
    ASSERT_STREQ(ss.str().c_str(), "124536897");
    std::cout << '\n';
}

TEST_F(BinaryNodeIteratorTests, ForEachIterator)
{
    auto begin = _tree->preOrderBegin();
    auto end = _tree->preOrderEnd();
    std::stringstream ss;
    std::for_each(begin, end, [&ss](const auto& bin_node){
        auto node = std::dynamic_pointer_cast<SymbolNode>(bin_node);
        ss << node->value();
    });
    ASSERT_STREQ(ss.str().c_str(), "124536897");
    std::cout << '\n';
}

TEST_F(BinaryNodeIteratorTests, Tagging)
{
    std::for_each(_tree->preOrderBegin(), _tree->preOrderEnd(), [](const auto &bin_node) {
        std::shared_ptr<SymbolNode> node = std::dynamic_pointer_cast<SymbolNode>(bin_node);
    });
}

