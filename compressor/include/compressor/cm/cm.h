#ifndef Cm_h
#define Cm_h

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <vector>

#include <compressor/error.h>

namespace compressor
{

inline constexpr unsigned cm_default_table_log2 = 24;
inline constexpr unsigned cm_min_table_log2 = 16;
inline constexpr unsigned cm_max_table_log2 = 26;

// Context mixing in the style of lpaq: bit predictions from order-0 to order-6 and word contexts
// and a match model, mixed by a gated logistic mixer, refined by two APMs and coded with a binary
// arithmetic coder. `table_log2` is the size in bytes of the hashed contexts' table. A pipeline
// coder (docs/file_format.md, "CM").
// With a `dictionary`, the model first runs over it without coding it (about 1-2 MB/s, on both
// sides), and the match model may point into it.
std::vector<uint8_t> cm_encode(std::span<const uint8_t> bytes,
                               unsigned table_log2 = cm_default_table_log2,
                               std::span<const uint8_t> dictionary = {});
// Appends exactly `size` bytes; corrupt_payload on a bad table size or a stream that ends early or
// leaves bytes over.
std::expected<void, Error> cm_decode(std::span<const uint8_t> payload, std::vector<uint8_t>& out,
                                     std::size_t size, std::span<const uint8_t> dictionary = {});

// BWT, then a context model made for its output (orders 0-2 and the run of the previous byte,
// mixed and refined by APMs) with the same arithmetic coder, in the style of bcm. A pipeline
// coder (docs/file_format.md, "BWT + CM"): the primary index (4 bytes, little-endian), then the
// coded transform.
std::vector<uint8_t> bwt_cm_encode(std::span<const uint8_t> bytes);
// Appends exactly `size` bytes; corrupt_payload on anything malformed.
std::expected<void, Error> bwt_cm_decode(std::span<const uint8_t> payload,
                                         std::vector<uint8_t>& out, std::size_t size);

} // namespace compressor

#endif /* Cm_h */
