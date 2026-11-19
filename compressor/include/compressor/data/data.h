#ifndef Data_h
#define Data_h

#include <compressor/huffman/huffman.h>
#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <vector>

namespace compressor
{

struct IData
{
};

struct DecodedData : IData
{
    std::vector<uint8_t> data_;
};

std::ostream& operator<<(std::ostream& ostream, const DecodedData& data);
std::istream& operator>>(std::istream& istream, DecodedData& data);
bool operator==(const DecodedData& lhs, const DecodedData& rhs);

// The values are stored in the file; 2-255 are reserved for later coders.
enum class Method : uint8_t
{
    stored = 0,
    huffman = 1,
};

struct EncodedData : IData
{
    Method method_ = Method::huffman;
    uint64_t size_ = 0;          // decoded size in bytes
    CodeLengths code_lengths_{}; // all zero when stored
    // Huffman: codes first bit most significant, zero-padded to a byte. Stored: the input itself.
    std::vector<uint8_t> payload_;
};

constexpr std::size_t stored_header_bytes = 9;
constexpr std::size_t huffman_header_bytes = 137;

std::ostream& operator<<(std::ostream& ostream, const EncodedData& data);
std::istream& operator>>(std::istream& istream, EncodedData& data);
bool operator==(const EncodedData& lhs, const EncodedData& rhs);

} // namespace compressor

#endif /* Data_h */
