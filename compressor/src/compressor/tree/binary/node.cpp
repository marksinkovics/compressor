#include <compressor/tree/binary/node.h>

#include <compressor/bitset/bitset.h>

#include <utility>

namespace compressor
{

std::shared_ptr<BinaryNode> BinaryNode::left() const
{
    return _left;
}

void BinaryNode::setLeft(std::shared_ptr<BinaryNode> left)
{
    _left = std::move(left);
}

std::shared_ptr<BinaryNode> BinaryNode::right() const
{
    return _right;
}

void BinaryNode::setRight(std::shared_ptr<BinaryNode> right)
{
    _right = std::move(right);
}

std::weak_ptr<BinaryNode> BinaryNode::parent() const
{
    return _parent;
}

void BinaryNode::setParent(std::weak_ptr<BinaryNode> parent)
{
    _parent = std::move(parent);
}

bool BinaryNode::hasParent() const
{
    return !_parent.expired();
}

void BinaryNode::setTag(std::shared_ptr<compressor::bitset> tag)
{
    _tag = std::move(tag);
}

std::shared_ptr<compressor::bitset> BinaryNode::tag() const
{
    return _tag;
}

bool BinaryNode::operator==(const BinaryNode& rhs)
{
    return this->_left.get() == rhs._left.get()
        && this->_right.get() == rhs._right.get()
        && this->_parent.lock().get() == rhs._parent.lock().get();
}

bool BinaryNode::operator!=(const BinaryNode& rhs){
    return !(*this == rhs);
}

} // compressor
