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

// Coder 14: LZT with three repeat offsets (offset codes 0-2, most recent first, moved to the front
// when used; explicit offsets from code 3) and a cost-based parse over the whole block: `passes`
// parses, each priced by the one before, matches from hash chains `depth` deep (a quarter as far
// once a match of 24 bytes is found), any match of `nice` or more taken as it is.
// With `use_gpu`, blocks of 256 KiB and more get their match lists from the GPU when there is
// one (the same lists, so the same output).
std::vector<uint8_t> lzt_optimal_encode(std::span<const uint8_t> bytes, unsigned passes = 2,
                                        unsigned depth = 64, std::size_t nice = 128,
                                        bool use_gpu = true);
std::expected<void, Error> lzt_optimal_decode(std::span<const uint8_t> payload,
                                              std::vector<uint8_t>& out, std::size_t size);

} // namespace compressor

#endif /* Lzt_h */
