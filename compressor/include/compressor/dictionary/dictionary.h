#ifndef Dictionary_h
#define Dictionary_h

#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <span>
#include <vector>

#include <compressor/error.h>

namespace compressor
{

inline constexpr std::size_t max_dictionary_bytes = std::size_t{1} << 20U;
inline constexpr std::size_t default_dictionary_bytes = 112'640; // zstd --train's default

// Content that every block's LZ77 and LZR coders may match into, as if it came just before the
// block (docs/file_format.md, "Dictionaries"). A dictionary file is these bytes as they are; the
// id that coded blocks record is their CRC32.
struct Dictionary
{
    std::vector<uint8_t> bytes;
    uint32_t id = 0;
};

Dictionary make_dictionary(std::vector<uint8_t> bytes);
// read_failed when the file can't be read, corrupt_header when it is empty or over
// max_dictionary_bytes.
std::expected<Dictionary, Error> read_dictionary(const std::filesystem::path& path);

// At most `size` bytes of what the samples share, in the style of zstd's COVER: 8-byte strings
// are scored by how many samples contain them, the samples' concatenation is cut into one epoch
// per 1 KiB of dictionary, and each epoch gives its best-scoring 1 KiB segment, whose strings
// then score nothing. The best segments come last, nearest to the data. Samples that together are
// smaller than `size` are the dictionary themselves.
std::vector<uint8_t> train_dictionary(std::span<const std::vector<uint8_t>> samples,
                                      std::size_t size = default_dictionary_bytes);

} // namespace compressor

#endif /* Dictionary_h */
