#ifndef Bwt_h
#define Bwt_h

#include <cstdint>
#include <expected>
#include <span>
#include <vector>

#include <compressor/error.h>

namespace compressor
{

// The suffix array of `bytes`: the start of every suffix, in sorted order (a suffix before every
// longer one it prefixes). SA-IS, linear time.
std::vector<uint32_t> suffix_array(std::span<const uint8_t> bytes);

// Burrows-Wheeler transform with an implicit end marker smaller than every byte: `out` gets the
// byte before each sorted suffix, skipping the suffix that is the whole input; the result is the
// row where that suffix was, which inverse_bwt() needs.
uint32_t forward_bwt(std::span<const uint8_t> bytes, std::vector<uint8_t>& out);
// Appends the original bytes to `out`. Fails with corrupt_payload when `primary` is out of range.
std::expected<void, Error> inverse_bwt(std::span<const uint8_t> transformed, uint32_t primary,
                                       std::vector<uint8_t>& out);

// The bzip2 approach as a pipeline coder (docs/file_format.md, "Pipeline blocks"): BWT, then
// move-to-front, zero runs written in bijective base 2, and static rANS over the result (it beat
// one Huffman code on every Silesia file, by 0.3-1.1%).
std::vector<uint8_t> bwt_encode(std::span<const uint8_t> bytes);
// Appends exactly `size` bytes; corrupt_payload on anything malformed.
std::expected<void, Error> bwt_decode(std::span<const uint8_t> payload, std::vector<uint8_t>& out,
                                      std::size_t size);

// The coder's two halves around the transform, for the FM-index (fmindex/), which stores a BWT it
// computed itself and searches it without inverting it.
std::vector<uint8_t> bwt_encode_transformed(std::span<const uint8_t> transformed, uint32_t primary);
// Fills `transformed` with `size` bytes and returns the primary index.
std::expected<uint32_t, Error> bwt_decode_transformed(std::span<const uint8_t> payload,
                                                      std::vector<uint8_t>& transformed,
                                                      std::size_t size);

} // namespace compressor

#endif /* Bwt_h */
