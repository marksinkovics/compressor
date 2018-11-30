#ifndef SymbolNode_hpp
#define SymbolNode_hpp

#include <tree/binary/node.h>

class SymbolNode: public BinaryNode
{
public:
    SymbolNode(const std::string &value, uint64_t frequency = 1);
    
    std::string value() const;
    uint64_t frequency() const;
    SymbolNode& operator++();
    
    // Relational operators
    inline friend bool operator< (const SymbolNode& lhs, const SymbolNode& rhs);
    inline friend bool operator> (const SymbolNode& lhs, const SymbolNode& rhs);
    inline friend bool operator<=(const SymbolNode& lhs, const SymbolNode& rhs);
    inline friend bool operator>=(const SymbolNode& lhs, const SymbolNode& rhs);
private:
    std::string _value;
    uint64_t _frequency;
};

#endif // SymbolNode_hpp
