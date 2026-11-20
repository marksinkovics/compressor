#ifndef Tans_h
#define Tans_h

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <vector>

#include <compressor/error.h>

namespace compressor
{

// Static order-0 tANS (table-based asymmetric numeral systems, as in zstd's FSE) over bytes: the
// frequencies scaled to 2^tans_table_log, a decoding table of that many states, and two
// interleaved states over one bit stream. Decoding is a table lookup and a bit read per byte.
inline constexpr unsigned tans_table_log = 11;

// Appends the frequency table and the coded bytes to `out`.
void tans_encode(std::span<const uint8_t> bytes, std::vector<uint8_t>& out);
// Appends exactly `count` bytes to `out`. Fails with corrupt_payload on a bad table, a stream that
// ends early or bytes left over; `out` is then unchanged.
std::expected<void, Error> tans_decode(std::span<const uint8_t> in, std::size_t count,
                                       std::vector<uint8_t>& out);

} // namespace compressor

#endif /* Tans_h */
