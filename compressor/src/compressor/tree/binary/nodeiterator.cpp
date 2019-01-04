#include <compressor/tree/binary/nodeiterator.h>

namespace compressor
{

BinaryNodeIterator::BinaryNodeIterator(std::shared_ptr<BinaryNode> node, uint64_t index)
    : _node(node)
    , _index(index)
{
    
}

std::shared_ptr<BinaryNode> BinaryNodeIterator::operator*() const
{
    return _node;
}

// Pre- and post-incrementable.
BinaryNodeIterator& BinaryNodeIterator::operator++()
{
    return *this;
}

BinaryNodeIterator BinaryNodeIterator::operator++(int)
{
    return BinaryNodeIterator(_node, _index);
}

// Pre- and post-decrementable.
BinaryNodeIterator& BinaryNodeIterator::operator--()
{
    return *this;
}

BinaryNodeIterator BinaryNodeIterator::operator--(int)
{
    return BinaryNodeIterator(_node, _index);
}

// Equality / inequality.
bool BinaryNodeIterator::operator==(const BinaryNodeIterator& rhs) const
{
    return *_node == *(rhs._node) && _index == rhs._index;
}

bool BinaryNodeIterator::operator!=(const BinaryNodeIterator& rhs) const
{
    return !(*this == rhs);
}

BinaryNodePreOrderIterator::BinaryNodePreOrderIterator(std::shared_ptr<BinaryNode> node, uint64_t index)
    : BinaryNodeIterator(node, index)
{
    
}

// Pre- and post-incrementable.
BinaryNodePreOrderIterator& BinaryNodePreOrderIterator::operator++()
{
    if (_node->left())
    {
        _node = _node->left();
        _index++;
        return *this;
    }
    
    auto node = _node;
    while(!node->parent().expired() && (node->parent().lock()->right() == nullptr || node->parent().lock()->right() == node))
    {
        node = node->parent().lock();
    }
    
    if (!node->parent().expired())
    {
        _node = node->parent().lock()->right();
    }
    
    _index++;
    return *this;
}

BinaryNodePreOrderIterator BinaryNodePreOrderIterator::operator++(int)
{
    BinaryNodePreOrderIterator it(*this);
    operator++();
    return it;
}

//// Pre- and post-decrementable.
//BinaryNodePreOrderIterator& BinaryNodePreOrderIterator::operator--()
//{
//
//}
//
//BinaryNodePreOrderIterator BinaryNodePreOrderIterator::operator--(int)
//{
//    BinaryNodePreOrderIterator it(*this);
//    operator--();
//    return it;
//
//}


} // compressor
