#ifndef BitVector_hpp
#define BitVector_hpp

#include <iostream>
#include <fstream>
#include <vector>

#include <limits>

class BitVectorIterator;

class BitVector
{
public:
    using DATA_TYPE = uint8_t;
    static const size_t BITS_PER_BYTE = (std::numeric_limits<DATA_TYPE>::digits);
    using value_type = bool;
    using iterator = BitVectorIterator;
public:
    BitVector(const std::size_t& bits = 0);
    BitVector(const std::string& bitstring);
    BitVector(const BitVector& v);
    BitVector& operator=(const BitVector& rhs);
    BitVector(BitVector&& v) noexcept;
    BitVector& operator=(BitVector&& rhs);
    
    BitVector& operator=(const std::string& bitstring);
    
    ~BitVector();
    
    // Read & Write operators
    
    void set(const std::size_t& pos, bool value = true) noexcept;
    void reset(const std::size_t& pos) noexcept;
    bool test(const std::size_t pos) const noexcept;
    bool operator[](const std::size_t& pos) const noexcept;
    
    void push_back(bool bit) noexcept;
    void append(const BitVector& vector) noexcept;
    
    void resize(const std::size_t& bits) noexcept;
    
    // Measuring methods
    
    std::size_t size() const noexcept;
    std::size_t num_bytes() const noexcept;
    
    // Relational operators
    
    bool operator==(const BitVector& rhs) const noexcept;
    bool operator!=(const BitVector& rhs) const noexcept;
    bool operator< (const BitVector& rhs) const noexcept;
    bool operator> (const BitVector& rhs) const noexcept;
    bool operator<=(const BitVector& rhs) const noexcept;
    bool operator>=(const BitVector& rhs) const noexcept;
    
    // Convert methods
    
    std::string str() const noexcept;
    
    // Iterators
    
    BitVectorIterator begin() const noexcept;
    BitVectorIterator end() const noexcept;

private:
    std::size_t bit_size;
    std::size_t byte_size;
    std::vector<DATA_TYPE> blocks;
public:
    friend std::ostream& operator<<(std::ostream& ostream, const BitVector& bitVector);
    friend std::istream& operator>>(std::istream& istream, BitVector& bitVector);
};

#endif /* BitVector_hpp */
