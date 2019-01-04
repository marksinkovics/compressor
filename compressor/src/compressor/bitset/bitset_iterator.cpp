#include <compressor/bitset/bitset_iterator.h>
#include <compressor/bitset/bitset.h>

namespace compressor
{

bitset_iterator::bitset_iterator(const bitset& vector, const std::size_t index)
{
    this->internal_bitset = &vector;
    this->internal_bit_index = index;
}

// Dereferencable.
bool bitset_iterator::operator*() const
{
    if (this->internal_bitset && this->internal_bit_index < this->internal_bitset->size()) {
        return this->internal_bitset->test(this->internal_bit_index);
    }
    return false;
}

// Pre-incrementable: ++it.
bitset_iterator& bitset_iterator::operator++()
{
    ++(this->internal_bit_index);
    return *this;
}

// Post-incrementable: it++.
bitset_iterator bitset_iterator::operator++(int)
{
    bitset_iterator it(*this);
    operator++();
    return it;
}

bitset_iterator& bitset_iterator::operator+=(int value)
{
    this->internal_bit_index += value;
    return *this;
}

bitset_iterator operator+(bitset_iterator& it, int value)
{
    it += value;
    return it;
}

// Pre-decrementable: --it.
bitset_iterator& bitset_iterator::operator--()
{
    --(this->internal_bit_index);
    return *this;
}

// Post-decrementable: it--.
bitset_iterator bitset_iterator::operator--(int)
{
    bitset_iterator it(*this);
    operator--();
    return it;
}

bitset_iterator& bitset_iterator::operator-=(int value)
{
    this->internal_bit_index -= value;
    return *this;
}

bitset_iterator operator-(bitset_iterator& it, int value)
{
    it -= value;
    return it;
}

// Equality: it == end().
bool bitset_iterator::operator==(const bitset_iterator& rhs) const
{
    return internal_bit_index == rhs.internal_bit_index;
}

// Inequality: it != end().
bool bitset_iterator::operator!=(const bitset_iterator& rhs) const
{
    return !(*this == rhs);
}

} // compressor
