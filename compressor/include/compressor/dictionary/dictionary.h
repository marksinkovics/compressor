#ifndef Dictionary_h
#define Dictionary_h

#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <optional>
#include <span>
#include <vector>

#include <compressor/deflate/deflate.h>
#include <compressor/error.h>

namespace compressor
{

inline constexpr std::size_t max_dictionary_bytes = std::size_t{1} << 20U;
inline constexpr std::size_t default_dictionary_bytes = 112'640; // zstd --train's default

// Content that every block's LZ77, LZR and CM coders may learn from, as if it came just before
// the block, and optionally Huffman codes for the LZ77 coder (docs/file_format.md,
// "Dictionaries"). A dictionary file is either raw content, or what write_dictionary_file()
// makes: a header with the codes, then the content. Coded blocks record the file's CRC32 as id.
struct Dictionary
{
    std::vector<uint8_t> bytes;
    uint32_t id = 0;
    std::optional<DeflateCodeLengths> codes;
};

inline constexpr std::size_t dictionary_header_bytes = 5 + 143 + 15;

// Raw content.
Dictionary make_dictionary(std::vector<uint8_t> bytes);
// A dictionary file's bytes: with the header and valid codes, structured; otherwise raw content.
// corrupt_header when empty or the content is over max_dictionary_bytes.
std::expected<Dictionary, Error> parse_dictionary(std::vector<uint8_t> file);
// read_failed when the file can't be read, else parse_dictionary().
std::expected<Dictionary, Error> read_dictionary(const std::filesystem::path& path);
// The header (magic, version, the codes' lengths as nibbles), then the content.
std::vector<uint8_t> write_dictionary_file(std::span<const uint8_t> content,
                                           const DeflateCodeLengths& codes);

// At most `size` bytes of what the samples share, in the style of zstd's COVER: 8-byte strings
// are scored by how many samples contain them, the samples' concatenation is cut into one epoch
// per 1 KiB of dictionary, and each epoch gives its best-scoring 1 KiB segment, whose strings
// then score nothing. The best segments come last, nearest to the data. Samples that together are
// smaller than `size` are the dictionary themselves.
std::vector<uint8_t> train_dictionary(std::span<const std::vector<uint8_t>> samples,
                                      std::size_t size = default_dictionary_bytes);
// Codes for the LZ77 coder: every sample matched against `content` as level 6 matches it, each
// symbol counted once more so every one has a code, limited to 15 bits.
DeflateCodeLengths train_codes(std::span<const std::vector<uint8_t>> samples,
                               std::span<const uint8_t> content);
// train_dictionary(), train_codes(), then write_dictionary_file(); empty when the samples share
// nothing.
std::vector<uint8_t> train_dictionary_file(std::span<const std::vector<uint8_t>> samples,
                                           std::size_t size = default_dictionary_bytes);

} // namespace compressor

#endif /* Dictionary_h */
