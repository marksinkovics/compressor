#ifndef Huffman_h
#define Huffman_h

#include <array>
#include <cstddef>
#include <cstdint>

namespace compressor
{

inline constexpr std::size_t symbol_count = 256;
// Fits a 4-bit length field and keeps the decoder's lookup table at 2^12 entries.
inline constexpr std::size_t max_code_length = 12;

using Frequencies = std::array<uint64_t, symbol_count>;
using CodeLengths = std::array<uint8_t, symbol_count>;

struct Code
{
    uint32_t bits = 0; // the code in the low `length` bits, first bit most significant
    uint8_t length = 0;

    bool operator==(const Code&) const = default;
};

using Codes = std::array<Code, symbol_count>;

// Optimal code lengths no longer than max_length (package-merge). A single symbol gets length 1.
CodeLengths code_lengths(const Frequencies& frequencies, std::size_t max_length = max_code_length);

// Canonical codes (RFC 1951, 3.2.2): ordered by (length, symbol). Lengths must be valid.
Codes canonical_codes(const CodeLengths& lengths);

// True for all-zero lengths, a single length-1 symbol, or a complete prefix code (Kraft sum 1)
// with no length above max_code_length.
bool valid_code_lengths(const CodeLengths& lengths);

} // namespace compressor

#endif /* Huffman_h */
