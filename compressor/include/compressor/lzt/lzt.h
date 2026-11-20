#ifndef Lzt_h
#define Lzt_h

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <vector>

#include <compressor/error.h>
#include <compressor/lz77/lz77.h>

namespace compressor
{

// LZ77 with `options`' window (up to the whole block), coded the way zstd codes sequences: each
// match with the literals before it is a sequence (literal run, match length, offset or "the last
// offset again"); the literals and the three kinds of codes go to four order-0 tANS streams, and
// the codes' extra bits to a raw bit stream. Decoding is four table lookups per sequence and
// copies. A pipeline coder (docs/file_format.md, "LZT").
std::vector<uint8_t> lzt_encode(std::span<const uint8_t> bytes, const Lz77Options& options);
// Appends exactly `size` bytes; corrupt_payload on anything malformed.
std::expected<void, Error> lzt_decode(std::span<const uint8_t> payload, std::vector<uint8_t>& out,
                                      std::size_t size);

} // namespace compressor

#endif /* Lzt_h */
