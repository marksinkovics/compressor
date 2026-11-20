#ifndef Inflate_h
#define Inflate_h

#include <cstddef>
#include <cstdint>
#include <expected>
#include <iosfwd>
#include <span>
#include <vector>

#include <compressor/error.h>

namespace compressor
{

// Decodes one raw Deflate stream (RFC 1951) and appends exactly `size` bytes to `out`. Matches may
// only reach back into what this stream produced, and into the last 32 KiB of `dictionary`, as if
// it came just before. Fails with corrupt_payload on anything malformed: a bad header or code, a
// distance too far back, more or fewer bytes than `size`, or bytes left after the final block.
std::expected<void, Error> inflate_bytes(std::span<const uint8_t> in, std::vector<uint8_t>& out,
                                         std::size_t size,
                                         std::span<const uint8_t> dictionary = {});

// Decodes deflate_with_codes()' stream: tokens coded with the given code lengths (no block
// header), the end-of-block code, then zero bits to a byte, nothing after. corrupt_payload for
// invalid codes or anything inflate_bytes() rejects.
std::expected<void, Error> inflate_with_codes(std::span<const uint8_t> in,
                                              std::vector<uint8_t>& out, std::size_t size,
                                              std::span<const uint8_t> history,
                                              std::span<const uint8_t> literal_lengths,
                                              std::span<const uint8_t> distance_lengths);

// Decodes a gzip file (RFC 1952), one member after another until the end of `in`, into `out`, and
// returns the bytes written. Each member's CRC32 and size are checked; a header CRC too when
// present. not_ingot when the first bytes aren't gzip's magic.
std::expected<uint64_t, Error> gunzip(std::istream& in, std::ostream& out);

} // namespace compressor

#endif /* Inflate_h */
