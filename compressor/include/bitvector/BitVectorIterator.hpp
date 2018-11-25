#ifndef BitVectorIterator_h
#define BitVectorIterator_h

#include <iostream>

class BitVector;

class BitVectorIterator {
public:
    //http://anderberg.me/2016/07/04/c-custom-iterators/
    
    // Default constructible.
    BitVectorIterator() = default;
    explicit BitVectorIterator(const BitVector& vector, const size_t index);
    
    bool operator*() const;
    
    // Pre- and post-incrementable.
    BitVectorIterator& operator++();
    BitVectorIterator operator++(int);
    BitVectorIterator& operator+=(int);
    friend BitVectorIterator operator+(BitVectorIterator&, int);
    
    // Pre- and post-decrementable.
    BitVectorIterator& operator--();
    BitVectorIterator operator--(int);
    BitVectorIterator& operator-=(int);
    friend BitVectorIterator operator-(BitVectorIterator&, int);
    
    // Equality / inequality.
    bool operator==(const BitVectorIterator& rhs) const;
    bool operator!=(const BitVectorIterator& rhs) const;
private:
    size_t internal_bit_index;
    const BitVector* internal_bitvector;
};

#endif /* BitVectorIterator_h */
