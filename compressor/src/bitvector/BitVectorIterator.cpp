#include <bitvector/bitvectoriterator.h>
#include <bitvector/bitvector.h>

BitVectorIterator::BitVectorIterator(const BitVector& vector, const std::size_t index)
{
    this->internal_bitvector = &vector;
    this->internal_bit_index = index;
}

// Dereferencable.
bool BitVectorIterator::operator*() const
{
    if (this->internal_bitvector && this->internal_bit_index < this->internal_bitvector->size()) {
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
    BitVectorIterator it(*this);
    operator++();
    return it;
}

BitVectorIterator& BitVectorIterator::operator+=(int value)
{
    this->internal_bit_index += value;
    return *this;
}

BitVectorIterator operator+(BitVectorIterator& it, int value)
{
    it += value;
    return it;
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
    BitVectorIterator it(*this);
    operator--();
    return it;
}

BitVectorIterator& BitVectorIterator::operator-=(int value)
{
    this->internal_bit_index -= value;
    return *this;
}

BitVectorIterator operator-(BitVectorIterator& it, int value)
{
    it -= value;
    return it;
}

// Equality: it == end().
bool BitVectorIterator::operator==(const BitVectorIterator& rhs) const
{
    return internal_bit_index == rhs.internal_bit_index;
}

// Inequality: it != end().
bool BitVectorIterator::operator!=(const BitVectorIterator& rhs) const
{
    return !(*this == rhs);
}

