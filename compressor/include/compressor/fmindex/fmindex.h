#ifndef FmIndex_h
#define FmIndex_h

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <iosfwd>
#include <span>
#include <string_view>
#include <vector>

#include <compressor/error.h>

namespace compressor
{

// An FM-index over an archive's decoded data (docs/file_format.md, "Search index"): the data in
// chunks that overlap by fm_overlap bytes, each stored as its BWT (the BWT coder's payload) and the
// text position of every fm_sample_rate-th suffix. Finding a pattern takes time in its length, not
// the data's; listing each match walks back to the nearest sample.
inline constexpr uint32_t fm_chunk_bytes = uint32_t{1} << 20U;
inline constexpr uint32_t fm_overlap = 4096; // the longest pattern searched
inline constexpr uint32_t fm_sample_rate = 64;
inline constexpr std::array<uint8_t, 4> search_index_magic{0x89, 'F', 'M', 'I'};
inline constexpr std::size_t search_trailer_bytes = 16;

// Collects the data as it is archived and builds each chunk once complete.
class FmIndexBuilder
{
public:
    void add(std::span<const uint8_t> bytes);
    // The section and its trailer; `section_offset` is where the section starts in the file.
    void write(std::ostream& out, uint64_t section_offset);

private:
    void build_chunk(std::span<const uint8_t> data);

    std::vector<uint8_t> pending_;
    std::vector<std::vector<uint8_t>> chunks_;
};

struct FmIndex
{
    uint64_t total_size = 0;
    std::vector<std::vector<uint8_t>> chunks; // serialised, checked when searched
};

// Reads the section between `section_offset` and the trailer, checking its CRC32.
std::expected<FmIndex, Error> read_search_index(std::istream& in, uint64_t section_offset,
                                                uint64_t section_end, uint64_t total_size);

// Where `pattern` (1 to fm_overlap bytes) starts in the data, in order; chunks are searched on
// `threads` threads (0: all cores).
std::expected<std::vector<uint64_t>, Error> fm_find(const FmIndex& index, std::string_view pattern,
                                                    unsigned threads = 0);

} // namespace compressor

#endif /* FmIndex_h */
