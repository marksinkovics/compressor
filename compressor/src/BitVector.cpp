//
//  BitVector.cpp
//  HuffmanLib
//
//  Created by Mark Sinkovics on 2018. 06. 16..
//  Copyright © 2018. Mark Sinkovics. All rights reserved.
//

#include "BitVector.hpp"

#include <algorithm>
#include <bitset>
#include <cmath>
#include <sstream>

BitVector::BitVector() : BitVector(1){
    
}

BitVector::BitVector(const size_t& size_in_bytes){
    this->internal_size_in_bytes = size_in_bytes;
    this->internal_array = new DATA_TYPE[this->internal_size_in_bytes];
    std::fill(this->internal_array, this->internal_array+this->internal_size_in_bytes, 0);
    this->bit_pointer = internal_size_in_bytes * BITS_PER_BYTE;

}

BitVector::BitVector(const BitVector& v){
    DATA_TYPE *new_internal_array = new DATA_TYPE[v.internal_size_in_bytes];
    for (size_t index = 0; index < v.internal_size_in_bytes; ++index) {
        new_internal_array[index] = v.internal_array[index];
    }
    
    this->internal_array = new_internal_array;
    this->internal_size_in_bytes = v.internal_size_in_bytes;
    this->bit_pointer = v.bit_pointer;
}

BitVector::~BitVector(){
    delete [] this->internal_array;
}

void BitVector::set(const size_t& pos) {
    this->set(pos, true);
}

void BitVector::reset(const size_t& pos) {
    this->set(pos, false);
}

bool BitVector::test(const size_t& pos) const {
    return this->get(pos);
}

void BitVector::set(const size_t &pos, const bool &data) {
    if (pos >= this->internal_size_in_bytes * sizeof(DATA_TYPE) * BITS_PER_BYTE) {
        std::cerr << "Attempted to access an illegal position." << std::endl;
        return;
    }
    
    if (pos > this->bit_pointer) {
        this->bit_pointer = pos;
    }
    
    size_t byte_index = (pos >> 3); //pos / BITS_PER_BYTE;
    size_t bit_offset = (pos & 7); //pos % BITS_PER_BYTE;

    if (data) {
        this->internal_array[byte_index] |= 1 << bit_offset;
    } else {
        this->internal_array[byte_index] &= ~(1 << bit_offset);
    }
}

bool BitVector::get(const size_t &pos) const {
    if (pos >= this->internal_size_in_bytes * sizeof(DATA_TYPE) * BITS_PER_BYTE) {
        std::cerr << "Attempted to access an illegal position." << std::endl;
        return false;
    }

    size_t byte_index = (pos >> 3); //pos / BITS_PER_BYTE;
    size_t bit_offset = (pos & 7); //pos % BITS_PER_BYTE;
    
    return (this->internal_array[byte_index] >> bit_offset) & 1;
}

bool BitVector::at(const size_t &pos) const {
    return this->get(pos);
}

bool BitVector::operator[](const size_t& pos) const {
    return this->get(pos);
}

BitVector& BitVector::operator=(const BitVector& rhs) {
    
    DATA_TYPE *new_internal_array = new DATA_TYPE[rhs.internal_size_in_bytes];
    
    for (size_t index = 0; index < rhs.internal_size_in_bytes; ++index) {
        new_internal_array[index] = rhs.internal_array[index];
    }
    
    delete [] this->internal_array;
    
    this->internal_array = new_internal_array;
    this->internal_size_in_bytes = rhs.internal_size_in_bytes;
    this->bit_pointer = rhs.bit_pointer;

    return *this;
}

std::string BitVector::to_string() const
{
    std::stringstream ss;
    size_t offset = this->bit_pointer % 8;
    for (size_t byte_index = 0; byte_index < this->internal_size_in_bytes; ++byte_index) {
        size_t i = (this->internal_size_in_bytes - byte_index - 1);
        DATA_TYPE byte_sector = this->internal_array[i];
        if (offset > 0 && byte_index == 0) {
            for (size_t bit_index = 0; bit_index < offset; ++bit_index) {
                ss << ((byte_sector >> (offset - bit_index - 1)) & 1);
            }
        } else {
            for (size_t bit_index = 0; bit_index < BITS_PER_BYTE; ++bit_index) {
                ss << ((byte_sector >> (BITS_PER_BYTE - bit_index - 1)) & 1);
            }
        }
    }
    return ss.str();
}

void BitVector::resize_in_bytes(const size_t &bytes){
    DATA_TYPE* new_internal_array = new DATA_TYPE[bytes];
    const size_t lower_size = std::min(this->internal_size_in_bytes, bytes);
    for (size_t i = 0; i < lower_size; ++i) {
        new_internal_array[i] = this->internal_array[i];
    }
    this->internal_size_in_bytes = bytes;
    delete [] this->internal_array;
    this->internal_array = new_internal_array;
}

void BitVector::resize_in_bits(const size_t &bits) {
    const size_t bytes = (bits >> 3) + ((bits & 7) ? 1 : 0);
    this->bit_pointer = bits;
    this->resize_in_bytes(bytes);
}

size_t BitVector::size_in_bits() const {
    return this->internal_size_in_bytes * sizeof(DATA_TYPE) * BITS_PER_BYTE;
}

size_t BitVector::size_in_bytes() const {
    return this->internal_size_in_bytes;
}

// Iterator

BitVectorIterator BitVector::begin() const {
    return BitVectorIterator(*this, 0);
}

BitVectorIterator BitVector::end() const {
    return BitVectorIterator(*this, this->bit_pointer);
}

// Streaming

std::ostream& operator<<(std::ostream& os, const BitVector& bitVector) {
    return os << bitVector.to_string();
}

//
//MARK: - BitVectorIterator
//

BitVectorIterator::BitVectorIterator(const BitVector& vector, const size_t index)
{
    this->internal_bitvector = &vector;
    this->internal_bit_index = index;
}

// Dereferencable.
bool BitVectorIterator::operator*() const
{
    if (this->internal_bitvector) {
        return this->internal_bitvector->test(this->internal_bit_index);
    }
    return false;
}

// Pre-incrementable: ++it.
BitVectorIterator& BitVectorIterator::operator++()
{
    ++(this->internal_bit_index);
    return *this;
}

// Post-incrementable: it++.
BitVectorIterator BitVectorIterator::operator++(int)
{
    (this->internal_bit_index)++;
    return *this;
}

// Pre-decrementable: --it.
BitVectorIterator& BitVectorIterator::operator--()
{
    --(this->internal_bit_index);
    return *this;
}

// Post-decrementable: it--.
BitVectorIterator BitVectorIterator::operator--(int)
{
    (this->internal_bit_index)--;
    return *this;
}

// Equality: it == end().
bool BitVectorIterator::operator==(const BitVectorIterator& rhs)
{
    return internal_bit_index == rhs.internal_bit_index;
}

// Inequality: it != end().
bool BitVectorIterator::operator!=(const BitVectorIterator& rhs)
{
    return !(*this == rhs);
}
