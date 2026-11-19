#ifndef Data_h
#define Data_h

#include <compressor/huffman/huffman.h>
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

struct EncodedData : IData
{
    uint64_t size_ = 0; // decoded size in bytes
    CodeLengths code_lengths_{};
    std::vector<uint8_t> payload_; // codes first bit most significant, zero-padded to a byte
};

std::ostream& operator<<(std::ostream& ostream, const EncodedData& data);
std::istream& operator>>(std::istream& istream, EncodedData& data);
bool operator==(const EncodedData& lhs, const EncodedData& rhs);

} // namespace compressor

#endif /* Data_h */
