#ifndef BinaryTree_hpp
#define BinaryTree_hpp

#include <memory>

class BinaryNode;
class BinaryNodePreOrderIterator;

class BinaryTree {
public:
    BinaryTree() = default;
    void setRoot(std::shared_ptr<BinaryNode> root);
    std::shared_ptr<BinaryNode> root() const;
    
    BinaryNodePreOrderIterator preOrderBegin() const;
    BinaryNodePreOrderIterator preOrderEnd() const;

private:
    
    static uint64_t numberOfChildren(std::shared_ptr<BinaryNode> node) noexcept;
    
    std::shared_ptr<BinaryNode> _root;
};

#endif /* BinaryTree_hpp */

