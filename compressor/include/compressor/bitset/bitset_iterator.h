#ifndef bitset_iterator_h
#define bitset_iterator_h

#include <iostream>

namespace compressor
{

class bitset;

class bitset_iterator {
public:
    //http://anderberg.me/2016/07/04/c-custom-iterators/
    
    // Default constructible.
    bitset_iterator() = default;
    explicit bitset_iterator(const bitset& vector, const std::size_t index);
    
    bool operator*() const;
    
    // Pre- and post-incrementable.
    bitset_iterator& operator++();
    bitset_iterator operator++(int);
    bitset_iterator& operator+=(int);
    friend bitset_iterator operator+(bitset_iterator&, int);
    
    // Pre- and post-decrementable.
    bitset_iterator& operator--();
    bitset_iterator operator--(int);
    bitset_iterator& operator-=(int);
    friend bitset_iterator operator-(bitset_iterator&, int);
    
    // Equality / inequality.
    bool operator==(const bitset_iterator& rhs) const;
    bool operator!=(const bitset_iterator& rhs) const;
private:
    std::size_t internal_bit_index;
    const bitset* internal_bitset;
};
    
} // compressor

#endif /* bitset_iterator_h */
