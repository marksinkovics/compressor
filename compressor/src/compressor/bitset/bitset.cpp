#include <compressor/bitset/bitset.h>

#include <algorithm>
#include <bitset>
#include <cmath>
#include <sstream>
#include <fstream>

#include <compressor/bitset/bitset_iterator.h>
#include <compressor/utils/utils.h>

namespace compressor
{

bitset::bitset(const std::size_t& bits)
{
    this->bit_size = bits;
    auto block_size = utils::calc_num_blocks(bits, BITS_PER_BLOCK);
    this->blocks.resize(block_size);
}

bitset::bitset(const std::string& bitstring)
    : bitset(bitstring.length())
{
    for (std::size_t sidx = 0, bidx = bitstring.length() - 1; sidx < bitstring.length(); ++sidx, --bidx)
    {
        if (bitstring[sidx] == '1')
            this->set(bidx);
    }
}

bitset::bitset(const bitset& v)
{
    this->bit_size = v.bit_size;
    this->blocks = v.blocks;
}

bitset::bitset(bitset&& v) noexcept
    : bit_size(std::exchange(v.bit_size, 0))
    , blocks(std::move(v.blocks))
{
    v.bit_size = 0;
    v.blocks.resize(0);
}

bitset::~bitset()
{
    
}

bitset& bitset::operator=(const bitset& rhs)
{
    if (&rhs == this)
        return *this;

    this->bit_size = rhs.bit_size;
    this->blocks = rhs.blocks;
    return *this;
}

bitset& bitset::operator=(bitset &&rhs)
{
    if (&rhs == this)
        return *this;
    
    this->bit_size = std::exchange(rhs.bit_size, 0);
    this->blocks = std::move(rhs.blocks);
    return *this;
}

bitset& bitset::operator=(const std::string& bitstring)
{
    if (this->str().compare(bitstring) == 0)
        return *this;

    this->bit_size = bitstring.length();
    auto block_size = utils::calc_num_blocks(this->bit_size, BITS_PER_BLOCK);
    this->blocks.resize(block_size);// = std::vector<block_type>(this->byte_size);
    
    for (std::size_t sidx = 0, bidx = bitstring.length() - 1; sidx < bitstring.length(); ++sidx, --bidx)
    {
        if (bitstring[sidx] == '1')
            this->set(bidx);
    }
    
    return *this;
}

void bitset::set(const std::size_t &pos, bool value) noexcept
{
    if (pos > this->bit_size) {
        utils::handle_illegal_position(pos);
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

void bitset::reset(const std::size_t& pos) noexcept
{
    this->set(pos, false);
}

bool bitset::test(const std::size_t pos) const noexcept
{
    if (pos > this->bit_size) {
        utils::handle_illegal_position(pos);
        return false;
    }
    return (this->blocks[(pos >> 3)] & (1 << (pos & 7))) != 0;
}

bool bitset::operator[](const std::size_t& pos) const noexcept
{
    return this->test(pos);
}

void bitset::push_back(bool bit) noexcept
{
    const std::size_t size = this->size();
    resize(size + 1);
    set(size, bit);
}

void bitset::append(const bitset& vector) noexcept
{
    const std::size_t size = this->size();
    resize(size + vector.size());
    for(std::size_t i = 0; i < vector.size(); ++i)
    {
        set(i + size, vector[i]);
    }
}

void bitset::resize(const std::size_t &bits) noexcept
{
    const auto block_size = utils::calc_num_blocks(bits, BITS_PER_BLOCK);
    this->bit_size = bits;
    if (block_size != this->blocks.size())
    {
        this->blocks.resize(block_size);
    }
}

// Relational operators

bool bitset::operator==(const bitset& rhs) const noexcept
{
    return this->size() == rhs.size() && this->blocks == rhs.blocks;
}

bool bitset::operator!=(const bitset& rhs) const noexcept
{
    return !(*this == rhs);
}

bool bitset::operator<(const bitset& rhs) const noexcept
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

bool bitset::operator>(const bitset& rhs) const noexcept
{
    return rhs < *this;
}

bool bitset::operator<=(const bitset& rhs) const noexcept
{
    return !(*this > rhs);
}

bool bitset::operator>=(const bitset& rhs) const noexcept
{
    return !(*this < rhs);
}

// Convert methods

std::string bitset::str() const noexcept
{
    std::stringstream stream;
    std::size_t fragment = this->bit_size % BITS_PER_BLOCK;
    for(auto it = blocks.rbegin(); it != blocks.rend(); ++it)
    {
        if (it == blocks.rbegin() && fragment > 0)
        {
            stream << utils::block_to_bit_string(*it, static_cast<block_type>(fragment));
        }
        else
        {
            stream << utils::block_to_bit_string(*it);
        }
    }

    return stream.str();
}

std::size_t bitset::size() const noexcept
{
    return this->bit_size;
}

std::size_t bitset::num_bytes() const noexcept
{
    return utils::calc_num_blocks(bit_size, BITS_PER_BYTE);
}

std::size_t bitset::num_blocks() const noexcept
{
    return blocks.size();
}

// Iterator

bitset_iterator bitset::begin() const noexcept
{
    return bitset_iterator(*this, 0);
}

bitset_iterator bitset::end() const noexcept
{
    return bitset_iterator(*this, this->bit_size);
}

std::ostream& operator<<(std::ostream& ostream, const bitset& bitset)
{
    ostream.write(reinterpret_cast<const char*>(&bitset.bit_size), sizeof(bitset.bit_size));
    if (bitset.bit_size > 0)
    {
        ostream.write(reinterpret_cast<const char*>(&(bitset.blocks[0])), bitset.blocks.size() * sizeof(bitset::block_type));
    }
    return ostream;
}

std::istream& operator>>(std::istream& istream, bitset& bitset)
{
    istream.read(reinterpret_cast<char*>(&bitset.bit_size), sizeof(bitset.bit_size));
    if (bitset.bit_size > 0)
    {
        const auto block_size = utils::calc_num_blocks(bitset.bit_size, bitset::BITS_PER_BLOCK);
        bitset.blocks.resize(block_size);
        for (std::size_t i = 0; i < block_size; ++i)
        {
            bitset::block_type value;
            istream.read(reinterpret_cast<char*>(&value), sizeof(bitset::block_type));
            bitset.blocks[i] = value;
        }
    }
    return istream;
}

} // compressor
