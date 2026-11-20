#ifndef Inflate_h
#define Inflate_h

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <vector>

#include <compressor/error.h>

namespace compressor
{

// Decodes one raw Deflate stream (RFC 1951) and appends exactly `size` bytes to `out`. Matches may
// only reach back into what this stream produced. Fails with corrupt_payload on anything
// malformed: a bad header or code, a distance too far back, more or fewer bytes than `size`, or
// bytes left after the final block.
std::expected<void, Error> inflate_bytes(std::span<const uint8_t> in, std::vector<uint8_t>& out,
                                         std::size_t size);

} // namespace compressor

#endif /* Inflate_h */
