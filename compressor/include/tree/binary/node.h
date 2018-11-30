#ifndef BinaryNode_hpp
#define BinaryNode_hpp

#include <iostream>

class BitVector;

class BinaryNode
{
public:
    explicit BinaryNode() = default;
    explicit BinaryNode(BinaryNode& node) = default;
    explicit BinaryNode(BinaryNode&& node) = default;
    virtual ~BinaryNode() = default;
    
    virtual BinaryNode& operator=(BinaryNode& node) = default;
    virtual BinaryNode& operator=(BinaryNode&& node) = default;
    
    virtual std::shared_ptr<BinaryNode> left() const;
    virtual void setLeft(std::shared_ptr<BinaryNode> left);
    virtual std::shared_ptr<BinaryNode> right() const;
    virtual void setRight(std::shared_ptr<BinaryNode> right);
    virtual std::weak_ptr<BinaryNode> parent() const;
    virtual void setParent(std::weak_ptr<BinaryNode> parent);
    virtual bool hasParent() const;
    
    virtual void setTag(std::shared_ptr<BitVector> tag);
    virtual std::shared_ptr<BitVector> tag() const;
    
    bool operator==(const BinaryNode& rhs);
    bool operator!=(const BinaryNode& rhs);
    
protected:
    std::shared_ptr<BinaryNode> _left;
    std::shared_ptr<BinaryNode> _right;
    std::weak_ptr<BinaryNode> _parent;
    std::shared_ptr<BitVector> _tag;
};

#endif /* BinaryNode_hpp */
