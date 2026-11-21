#ifndef Chunk_h
#define Chunk_h

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace compressor
{

inline constexpr std::size_t min_chunk_average = 64;
inline constexpr std::size_t max_chunk_average = std::size_t{1} << 20U;

// Content-defined chunking (FastCDC: a gear hash of about the last 64 bytes, normalised chunking).
// Each chunk is from average / 4 to average * 8 bytes, except that the last may be shorter. An edit
// moves only the boundaries near it, so repeated content splits into the same chunks wherever it
// is. Returns each chunk's end; the last is bytes.size(). `average`: a power of two from
// min_chunk_average to max_chunk_average.
std::vector<std::size_t> chunk_ends(std::span<const uint8_t> bytes, std::size_t average);

// A 64-bit hash for finding repeated chunks. Not cryptographic: compare the bytes before relying on
// a match.
uint64_t chunk_hash(std::span<const uint8_t> bytes);

} // namespace compressor

#endif /* Chunk_h */
