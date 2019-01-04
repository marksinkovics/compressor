#ifndef bitset_hpp
#define bitset_hpp

#include <iostream>
#include <fstream>
#include <vector>

#include <limits>

namespace compressor
{

class bitset_iterator;

class bitset
{
public:
    using DATA_TYPE = uint8_t;
    static const size_t BITS_PER_BYTE = (std::numeric_limits<DATA_TYPE>::digits);
    using value_type = bool;
    using iterator = bitset_iterator;
public:
    bitset(const std::size_t& bits = 0);
    bitset(const std::string& bitstring);
    bitset(const bitset& v);
    bitset& operator=(const bitset& rhs);
    bitset(bitset&& v) noexcept;
    bitset& operator=(bitset&& rhs);
    
    bitset& operator=(const std::string& bitstring);
    
    ~bitset();
    
    // Read & Write operators
    
    void set(const std::size_t& pos, bool value = true) noexcept;
    void reset(const std::size_t& pos) noexcept;
    bool test(const std::size_t pos) const noexcept;
    bool operator[](const std::size_t& pos) const noexcept;
    
    void push_back(bool bit) noexcept;
    void append(const bitset& vector) noexcept;
    
    void resize(const std::size_t& bits) noexcept;
    
    // Measuring methods
    
    std::size_t size() const noexcept;
    std::size_t num_bytes() const noexcept;
    
    // Relational operators
    
    bool operator==(const bitset& rhs) const noexcept;
    bool operator!=(const bitset& rhs) const noexcept;
    bool operator< (const bitset& rhs) const noexcept;
    bool operator> (const bitset& rhs) const noexcept;
    bool operator<=(const bitset& rhs) const noexcept;
    bool operator>=(const bitset& rhs) const noexcept;
    
    // Convert methods
    
    std::string str() const noexcept;
    
    // Iterators
    
    bitset_iterator begin() const noexcept;
    bitset_iterator end() const noexcept;

private:
    std::size_t bit_size;
    std::size_t byte_size;
    std::vector<DATA_TYPE> blocks;
public:
    friend std::ostream& operator<<(std::ostream& ostream, const bitset& bitset);
    friend std::istream& operator>>(std::istream& istream, bitset& bitset);
};

} // compressor

#endif /* bitset_hpp */
