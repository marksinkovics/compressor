#include <tree/binary/node.h>

#include <bitvector/bitvector.h>

std::shared_ptr<BinaryNode> BinaryNode::left() const
{
    return _left;
}

void BinaryNode::setLeft(std::shared_ptr<BinaryNode> left)
{
    _left = left;
}

std::shared_ptr<BinaryNode> BinaryNode::right() const
{
    return _right;
}

void BinaryNode::setRight(std::shared_ptr<BinaryNode> right)
{
    _right = right;
}

std::weak_ptr<BinaryNode> BinaryNode::parent() const
{
    return _parent;
}

void BinaryNode::setParent(std::weak_ptr<BinaryNode> parent)
{
    _parent = parent;
}

bool BinaryNode::hasParent() const
{
    return !_parent.expired();
}

void BinaryNode::setTag(std::shared_ptr<BitVector> tag)
{
    _tag = tag;
}

std::shared_ptr<BitVector> BinaryNode::tag() const
{
    return _tag;
}

bool BinaryNode::operator==(const BinaryNode& rhs)
{
    return this->_left.get() == this->_left.get()
        && this->_right.get() == this->_right.get()
        && this->_parent.lock().get() == this->_parent.lock().get();
}

bool BinaryNode::operator!=(const BinaryNode& rhs){
    return !(*this == rhs);
}
