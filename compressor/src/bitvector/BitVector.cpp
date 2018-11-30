#include <bitvector/bitvector.h>

#include <algorithm>
#include <bitset>
#include <cmath>
#include <sstream>
#include <fstream>

#include <bitvector/bitvectoriterator.h>
#include <compressor/utils/utils.h>

BitVector::BitVector(const std::size_t& bits)
{
    this->bit_size = bits;
    this->byte_size = Utils::calculate_byte_size(bits);
    this->blocks = std::vector<DATA_TYPE>(this->byte_size);
}

BitVector::BitVector(const std::string& bitstring)
    : BitVector(bitstring.length())
{
    for (std::size_t sidx = 0, bidx = bitstring.length() - 1; sidx < bitstring.length(); ++sidx, --bidx)
    {
        if (bitstring[sidx] == '1')
            this->set(bidx);
    }
}

BitVector::BitVector(const BitVector& v)
{
    this->bit_size = v.bit_size;
    this->byte_size = v.byte_size;
    this->blocks = v.blocks;
}

BitVector::BitVector(BitVector&& v) noexcept
    : bit_size(std::exchange(v.bit_size, 0))
    , byte_size(std::exchange(v.byte_size, 0))
    , blocks(std::move(v.blocks))
{
    v.bit_size = 0;
    v.byte_size = 0;
    v.blocks.resize(0);
}

BitVector::~BitVector()
{
    
}

BitVector& BitVector::operator=(const BitVector& rhs)
{
    if (&rhs == this)
        return *this;

    this->bit_size = rhs.bit_size;
    this->byte_size = rhs.byte_size;
    this->blocks = rhs.blocks;
    return *this;
}

BitVector& BitVector::operator=(BitVector &&rhs)
{
    if (&rhs == this)
        return *this;
    
    this->bit_size = std::exchange(rhs.bit_size, 0);
    this->byte_size = std::exchange(rhs.byte_size, 0);
    this->blocks = std::move(rhs.blocks);
    return *this;
}

BitVector& BitVector::operator=(const std::string& bitstring)
{
    if (this->str().compare(bitstring) == 0)
        return *this;

    this->bit_size = bitstring.length();
    this->byte_size = Utils::calculate_byte_size(this->bit_size);
    this->blocks = std::vector<DATA_TYPE>(this->byte_size);
    
    for (std::size_t sidx = 0, bidx = bitstring.length() - 1; sidx < bitstring.length(); ++sidx, --bidx)
    {
        if (bitstring[sidx] == '1')
            this->set(bidx);
    }
    
    return *this;
}

void BitVector::set(const std::size_t &pos, bool value) noexcept
{
    if (pos > this->bit_size) {
        Utils::handle_illegal_position(pos);
        return;
    }
    
    std::size_t byte_index = (pos >> 3); //pos / BITS_PER_BYTE;
    std::size_t bit_offset = (pos & 7); //pos % BITS_PER_BYTE;
    if (value) {
        this->blocks[byte_index] |= 1 << bit_offset;
    } else {
        this->blocks[byte_index] &= ~(1 << bit_offset);
    }
}

void BitVector::reset(const std::size_t& pos) noexcept
{
    this->set(pos, false);
}

bool BitVector::test(const std::size_t pos) const noexcept
{
    if (pos > this->bit_size) {
        Utils::handle_illegal_position(pos);
        return false;
    }
    return (this->blocks[(pos >> 3)] & (1 << (pos & 7))) != 0;
}

bool BitVector::operator[](const std::size_t& pos) const noexcept
{
    return this->test(pos);
}

void BitVector::push_back(bool bit) noexcept
{
    const std::size_t size = this->size();
    resize(size + 1);
    set(size, bit);
}

void BitVector::append(const BitVector& vector) noexcept
{
    const std::size_t size = this->size();
    resize(size + vector.size());
    for(std::size_t i = 0; i < vector.size(); ++i)
    {
        set(i + size, vector[i]);
    }
}

void BitVector::resize(const std::size_t &bits) noexcept
{
    const std::size_t bytes = Utils::calculate_byte_size(bits);
    this->bit_size = bits;
    if (bytes != this->byte_size)
    {
        this->byte_size = bytes;
        this->blocks.resize(byte_size);
    }
}

// Relational operators

bool BitVector::operator==(const BitVector& rhs) const noexcept
{
    return this->size() == rhs.size() && this->blocks == rhs.blocks;
}

bool BitVector::operator!=(const BitVector& rhs) const noexcept
{
    return !(*this == rhs);
}

bool BitVector::operator<(const BitVector& rhs) const noexcept
{
    std::size_t asize = this->size();
    std::size_t bsize = rhs.size();
    
    if (!bsize)
    {
        return false;
    }
    else if (!asize)
    {
        return true;
    }
    else
    {
        if(asize == bsize)
        {
            for(std::size_t i = this->num_bytes(); i > 0; --i)
            {
                std::size_t j = i - 1;
                if (blocks[j] < rhs.blocks[j])
                {
                    return true;
                }
                else if (blocks[j]> rhs.blocks[j])
                {
                    return false;
                }
            }
            return false;
        }
        else
        {
            std::size_t minsize = std::min(asize, bsize);
            for (std::size_t i = 0; i < minsize; ++i, --asize,--bsize)
            {
                size_t a_index = asize - 1;
                size_t b_index = bsize - 1;
                if ((*this)[a_index] < rhs[b_index])
                {
                    return true;
                }
                else if ((*this)[a_index] > rhs[b_index])
                {
                    return false;
                }
            }
            return (this->size() < rhs.size());
        }
    }
}

bool BitVector::operator>(const BitVector& rhs) const noexcept
{
    return rhs < *this;
}

bool BitVector::operator<=(const BitVector& rhs) const noexcept
{
    return !(*this > rhs);
}

bool BitVector::operator>=(const BitVector& rhs) const noexcept
{
    return !(*this < rhs);
}

// ToString methods

std::string BitVector::str() const noexcept
{
    std::stringstream stream;
    std::size_t fragment = (this->bit_size & 7);
    for (std::size_t byte_index = this->byte_size; byte_index > 0; byte_index--)
    {
        DATA_TYPE byte = this->blocks[byte_index - 1];
        if (byte_index == this->byte_size && fragment > 0)
        {
            stream << Utils::byte_to_bit_string(byte, fragment);
        }
        else
        {
            stream << Utils::byte_to_bit_string(byte);
        }
    }
    return stream.str();
}

std::size_t BitVector::size() const noexcept
{
    return this->bit_size;
}

std::size_t BitVector::num_bytes() const noexcept
{
    return this->byte_size;
}

// Iterator

BitVectorIterator BitVector::begin() const noexcept
{
    return BitVectorIterator(*this, 0);
}

BitVectorIterator BitVector::end() const noexcept
{
    return BitVectorIterator(*this, this->bit_size);
}

std::ostream& operator<<(std::ostream& ostream, const BitVector& bitVector)
{
    ostream.write(reinterpret_cast<const char*>(&bitVector.bit_size), sizeof(bitVector.bit_size));
//    std::ostream_iterator<BitVector::DATA_TYPE> output_iterator(ostream);
//    std::copy(bitVector.blocks.begin(), bitVector.blocks.end(), output_iterator);
//    ostream.write(fd, &vector[0], vector.size() * sizeof(vector[0]));
    if (bitVector.bit_size > 0)
    {
        ostream.write(reinterpret_cast<const char*>(&(bitVector.blocks[0])), bitVector.blocks.size() * sizeof(BitVector::DATA_TYPE));
    }

//    for(const auto value : bitVector.blocks)
//    {
//        ostream.write(reinterpret_cast<const char*>(&value), sizeof(BitVector::DATA_TYPE));
//    }
    return ostream;
}

std::istream& operator>>(std::istream& istream, BitVector& bitVector)
{
    istream.read(reinterpret_cast<char*>(&bitVector.bit_size), sizeof(bitVector.bit_size));
    
    if (bitVector.bit_size > 0)
    {
        bitVector.byte_size = Utils::calculate_byte_size(bitVector.bit_size);
        bitVector.blocks.reserve(bitVector.byte_size);
        for (std::size_t i = 0; i < bitVector.byte_size; ++i)
        {
            BitVector::DATA_TYPE value;
            istream.read(reinterpret_cast<char*>(&value), sizeof(BitVector::DATA_TYPE));
            bitVector.blocks.push_back(value);
        }
    }
    
//    std::copy_n(std::istream_iterator<BitVector::DATA_TYPE>(istream),
//                bitVector.byte_size,
//                std::back_inserter(bitVector.blocks));
    return istream;
}
