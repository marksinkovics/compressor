//
//  BitVector.cpp
//  HuffmanLib
//
//  Created by Mark Sinkovics on 2018. 06. 16..
//  Copyright © 2018. Mark Sinkovics. All rights reserved.
//

#include "bitvector/BitVector.hpp"
#include "bitvector/BitVectorIterator.hpp"

#include <algorithm>
#include <bitset>
#include <cmath>
#include <sstream>
#include <fstream>

BitVector::BitVector() : BitVector(1){
    
}

BitVector::BitVector(const size_t& size_in_bytes){
    this->internal_size_in_bytes = size_in_bytes;
    this->internal_array = new DATA_TYPE[this->internal_size_in_bytes];
    std::fill(this->internal_array, this->internal_array+this->internal_size_in_bytes, 0);
    this->bit_pointer = internal_size_in_bytes * BITS_PER_BYTE - 1;
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
    if (pos >= this->internal_size_in_bytes * BITS_PER_BYTE) {
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

std::string BitVector::byte_to_bit_string(uint8_t byte, uint8_t length) const
{
    if (length < 0 || length > 8) {
        std::cerr << "Invalid length was given!\n";
        return "";
    }
    
    std::stringstream stream;
    for (size_t bit_index = 0; bit_index < length; bit_index++) {
        stream << ((byte >> (length - bit_index - 1)) & 0x1);
    }
    return stream.str();
}

std::string BitVector::to_string() const
{
    std::stringstream stream;
    size_t fragment = ((this->bit_pointer + 1) & 7);
    for (size_t byte_index = this->internal_size_in_bytes; byte_index > 0; byte_index--) {
        DATA_TYPE byte = this->internal_array[byte_index - 1];
        if (byte_index == this->internal_size_in_bytes && fragment > 0)
            stream << byte_to_bit_string(byte, fragment);
        else
            stream << byte_to_bit_string(byte);
    }
    return stream.str();
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
    this->bit_pointer = bits - 1;
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

std::ofstream& operator<<(std::ofstream& ofs, const BitVector& bitVector) {
    
    std::cout << "Size: " << bitVector.internal_size_in_bytes << '\n';
    ofs.write(reinterpret_cast<const char*>(&bitVector.internal_size_in_bytes), sizeof(size_t));
    std::cout << "Pointer: " << bitVector.bit_pointer << '\n';
    ofs.write(reinterpret_cast<const char*>(&bitVector.bit_pointer), sizeof(size_t));
    ofs.write(reinterpret_cast<const char*>(bitVector.internal_array), bitVector.internal_size_in_bytes);
    return ofs;
}

std::ifstream& operator>>(std::ifstream& ifs, BitVector& bitVector) {
    
    size_t size_in_bytes;
    size_t bit_pointer;
    
    ifs.read(reinterpret_cast<char*>(&size_in_bytes), sizeof(size_in_bytes));
    ifs.read(reinterpret_cast<char*>(&bit_pointer), sizeof(bit_pointer));

    if (bitVector.internal_array != nullptr) {
        delete [] bitVector.internal_array;
    }
    
    bitVector.bit_pointer = bit_pointer;
    bitVector.internal_size_in_bytes = size_in_bytes;
    bitVector.internal_array = new BitVector::DATA_TYPE[bitVector.internal_size_in_bytes];
    ifs.read(reinterpret_cast<char*>(bitVector.internal_array), bitVector.internal_size_in_bytes);

    return ifs;
}
