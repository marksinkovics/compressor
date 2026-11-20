#ifndef Ppm_h
#define Ppm_h

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <vector>

#include <compressor/error.h>

namespace compressor
{

// Prediction by partial matching, PPMC: contexts of the last 5 bytes down to none, each with its
// bytes' counts; a byte not seen in a context is coded as an escape (counted as the context's
// number of distinct bytes) and looked up one order lower, excluding the bytes already ruled out,
// down to a uniform order -1. Coded with a carry-less range coder.
std::vector<uint8_t> ppm_encode(std::span<const uint8_t> bytes);
// Appends exactly `size` bytes; corrupt_payload on a stream that ends early or leaves bytes over.
std::expected<void, Error> ppm_decode(std::span<const uint8_t> payload, std::vector<uint8_t>& out,
                                      std::size_t size);

} // namespace compressor

#endif /* Ppm_h */
