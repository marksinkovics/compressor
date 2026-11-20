#ifndef Rans_h
#define Rans_h

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <vector>

#include <compressor/error.h>

namespace compressor
{

// Static order-0 rANS (Duda's asymmetric numeral systems, range variant) over an alphabet of up to
// 65536 symbols: the symbols' frequencies scaled to 2^15, then one 32-bit state renormalised a
// byte at a time. Codes within a fraction of a percent of the order-0 entropy, where Huffman
// spends at least one bit even on a symbol with probability 0.9.
inline constexpr unsigned rans_scale_bits = 15;

// Appends the frequency table and the coded symbols to `out`.
void rans_encode(std::span<const uint16_t> symbols, std::size_t alphabet,
                 std::vector<uint8_t>& out);
// Decodes `count` symbols. Fails with corrupt_payload on a bad table, a symbol outside the
// alphabet, a short stream or bytes left over.
std::expected<std::vector<uint16_t>, Error> rans_decode(std::span<const uint8_t> in,
                                                        std::size_t alphabet, std::size_t count);

} // namespace compressor

#endif /* Rans_h */
