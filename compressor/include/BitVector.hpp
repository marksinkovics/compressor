//
//  BitVector.hpp
//  HuffmanLib
//
//  Created by Mark Sinkovics on 2018. 06. 16..
//  Copyright © 2018. Mark Sinkovics. All rights reserved.
//

#ifndef BitVector_hpp
#define BitVector_hpp

#include <iostream>

class BitVectorIterator;

class BitVector {
public:
    typedef uint8_t DATA_TYPE;
    static const char BITS_PER_BYTE = 8;
public:
    BitVector();
    BitVector(const size_t& size_in_bytes);
    BitVector(const BitVector& v);
    ~BitVector();
    
    void set(const size_t& pos);
    void reset(const size_t& pos);
    bool test(const size_t& pos) const;
    
    void set(const size_t& pos, const bool& data);
    bool get(const size_t& pos) const;
    bool at(const size_t& pos) const;
    
    bool operator[](const size_t& pos) const;
    BitVector& operator=(const BitVector& rhs);
    
    std::string to_string() const;
    
    size_t size_in_bits() const;
    size_t size_in_bytes() const;
    
    void resize_in_bytes(const size_t& bytes);
    void resize_in_bits(const size_t& bits);
    
    // Iterator
    
    BitVectorIterator begin() const;
    BitVectorIterator end() const;
    
private:
    size_t internal_size_in_bytes;
    DATA_TYPE *internal_array;
    size_t bit_pointer;
};

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
    
    // Pre- and post-decrementable.
    BitVectorIterator& operator--();
    BitVectorIterator operator--(int);
    
    // Equality / inequality.
    bool operator==(const BitVectorIterator& rhs);
    bool operator!=(const BitVectorIterator& rhs);
private:
    size_t internal_bit_index;
    const BitVector* internal_bitvector;
};

std::ostream& operator<<(std::ostream& os, const BitVector& bitVector);

#endif /* BitVector_hpp */
