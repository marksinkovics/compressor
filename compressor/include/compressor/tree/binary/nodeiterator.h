#ifndef BinaryNodeIterator_hpp
#define BinaryNodeIterator_hpp

#include <compressor/tree/binary/node.h>
#include <memory>
#include <unordered_set>

namespace compressor
{

class BinaryNodeIterator
{
public:
    BinaryNodeIterator() = default;
    explicit BinaryNodeIterator(std::shared_ptr<BinaryNode> node, uint64_t index);

    std::shared_ptr<BinaryNode> operator*() const;

    // Pre- and post-incrementable.
    BinaryNodeIterator& operator++();
    BinaryNodeIterator operator++(int);

    // Pre- and post-decrementable.
    BinaryNodeIterator& operator--();
    BinaryNodeIterator operator--(int);

    // Equality / inequality.
    bool operator==(const BinaryNodeIterator& rhs) const;
    bool operator!=(const BinaryNodeIterator& rhs) const;

protected:
    std::shared_ptr<BinaryNode> _node;
    uint64_t _index;
};

class BinaryNodePreOrderIterator : public BinaryNodeIterator
{
public:
    BinaryNodePreOrderIterator(std::shared_ptr<BinaryNode> node, uint64_t numerOfNodes);
    // Shadows on purpose: each iterator returns its own type.
    // NOLINTBEGIN(bugprone-derived-method-shadowing-base-method)
    BinaryNodePreOrderIterator& operator++();
    BinaryNodePreOrderIterator operator++(int);
    // NOLINTEND(bugprone-derived-method-shadowing-base-method)

    //    // Pre- and post-decrementable.
    //    BinaryNodePreOrderIterator& operator--();
    //    BinaryNodePreOrderIterator operator--(int);
};

} // namespace compressor

#endif /* BinaryNodeIterator_hpp */
