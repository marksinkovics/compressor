#include <compressor/tree/binary/tree.h>

#include <compressor/tree/binary/node.h>
#include <compressor/tree/binary/nodeiterator.h>

#include <utility>

namespace compressor
{

void BinaryTree::setRoot(std::shared_ptr<BinaryNode> root)
{
    _root = std::move(root);
}

std::shared_ptr<BinaryNode> BinaryTree::root() const
{
    return _root;
}

uint64_t BinaryTree::numberOfChildren(const std::shared_ptr<BinaryNode>& node) noexcept
{
    uint64_t counter = 1;
    
    if (node->left())
        counter += numberOfChildren(node->left());
    
    if (node->right())
        counter += numberOfChildren(node->right());

    return counter;
}

BinaryNodePreOrderIterator BinaryTree::preOrderBegin() const
{
    return {_root, 1};
}

BinaryNodePreOrderIterator BinaryTree::preOrderEnd() const
{
    std::shared_ptr<BinaryNode> rightestNode = _root;
    
    while(rightestNode->right()) {
        rightestNode = rightestNode->right();
    }
    
    while(rightestNode->left()) {
        rightestNode = rightestNode->left();
    }
    
    uint64_t num = numberOfChildren(_root) + 1;

    return {rightestNode, num};
}
    
} // compressor
