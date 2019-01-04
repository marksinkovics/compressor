#include <compressor/tree/symbolnode.h>

namespace compressor
{

SymbolNode::SymbolNode(const std::string &value, uint64_t frequency)
    : BinaryNode()
    , _value(value)
    , _frequency(frequency)
{
}

std::string SymbolNode::value() const
{
    return _value;
}

uint64_t SymbolNode::frequency() const
{
    return _frequency;
}

SymbolNode& SymbolNode::operator++()
{
    ++_frequency;
    return *this;
}

inline bool operator< (const SymbolNode& lhs, const SymbolNode& rhs)
{
    return lhs.frequency() < rhs.frequency();
}

inline bool operator> (const SymbolNode& lhs, const SymbolNode& rhs)
{
    return rhs < lhs;
}

inline bool operator<=(const SymbolNode& lhs, const SymbolNode& rhs)
{
    return !(lhs > rhs);
}

inline bool operator>=(const SymbolNode& lhs, const SymbolNode& rhs)
{
    return !(lhs < rhs);
}

}
