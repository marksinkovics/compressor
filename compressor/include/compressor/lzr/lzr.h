#ifndef Lzr_h
#define Lzr_h

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <vector>

#include <compressor/error.h>
#include <compressor/lz77/lz77.h>

namespace compressor
{

// LZ77 over the whole block (a window as large as the block, unlike Deflate's 32 KiB) coded with
// LZMA's model and adaptive binary range coder: literals in the context of the previous byte and,
// after a match, of the byte at the last distance; the last distance repeated cheaply; distance
// slots and LZMA's three-tier lengths. A pipeline coder (docs/file_format.md, "LZR").
// With a `dictionary`, matches and the literal model's previous and match bytes may reach into it
// as if it came just before the block.
std::vector<uint8_t> lzr_encode(std::span<const uint8_t> block, const Lz77Options& options,
                                std::span<const uint8_t> dictionary = {});
// Appends exactly `size` bytes; corrupt_payload on a distance before the block (and dictionary),
// a stream that ends early or leaves bytes over.
std::expected<void, Error> lzr_decode(std::span<const uint8_t> payload, std::vector<uint8_t>& out,
                                      std::size_t size, std::span<const uint8_t> dictionary = {});

} // namespace compressor

#endif /* Lzr_h */
