#ifndef FmIndex_h
#define FmIndex_h

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <expected>
#include <filesystem>
#include <future>
#include <iosfwd>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

#include <compressor/error.h>
#include <compressor/thread/pool.h>

namespace compressor
{

// An FM-index over an archive's decoded data (docs/file_format.md, "Search index"): the data in
// chunks that overlap by fm_overlap bytes, each stored as its BWT in a wavelet matrix (eight
// bit-planes with rank directories) plus the text position of every fm_sample_rate-th suffix. It
// is searched in place through mmap: a query touches only the pages its rank lookups need, so it
// costs time in the pattern's length and the number of matches, not the data's size.
inline constexpr uint32_t fm_chunk_bytes = uint32_t{1} << 20U;
inline constexpr uint32_t fm_overlap = 4096; // the longest pattern searched
inline constexpr uint32_t fm_sample_rate = 64;
inline constexpr std::array<uint8_t, 4> search_index_magic{0x89, 'F', 'M', 'I'};
inline constexpr std::size_t search_trailer_bytes = 16;

// Collects the data as it is archived and builds each chunk on a thread pool, spooling finished
// chunks to a temporary file rather than keeping them in memory.
class FmIndexBuilder
{
public:
    // 0 threads: default_thread_count().
    explicit FmIndexBuilder(unsigned threads = 0);
    ~FmIndexBuilder();
    FmIndexBuilder(const FmIndexBuilder&) = delete;
    FmIndexBuilder& operator=(const FmIndexBuilder&) = delete;
    FmIndexBuilder(FmIndexBuilder&&) = delete;
    FmIndexBuilder& operator=(FmIndexBuilder&&) = delete;

    void add(std::span<const uint8_t> bytes);
    // The section and its trailer; `section_offset` is where the section starts in the file.
    // False when the spool file failed.
    bool write(std::ostream& out, uint64_t section_offset);

private:
    void submit(std::vector<uint8_t> data);
    bool spool_oldest();

    ThreadPool pool_;
    std::size_t max_in_flight_;
    std::deque<std::future<std::vector<uint8_t>>> in_flight_;
    std::vector<uint8_t> pending_;
    std::FILE* spool_ = nullptr;
    std::vector<uint64_t> chunk_sizes_;
    bool failed_ = false;
};

struct FmChunkView;

// A section opened for searching, over memory it keeps alive (a mapping or a buffer).
struct FmIndex
{
    std::shared_ptr<const void> owner;
    uint64_t total_size = 0;
    std::vector<std::span<const uint8_t>> chunks;
};

// Checks the section's layout and chunk table (not its CRC; see check_search_index()).
std::expected<FmIndex, Error> parse_search_index(std::span<const uint8_t> section,
                                                 uint64_t total_size,
                                                 std::shared_ptr<const void> owner);
// Maps the section of `archive` between `section_offset` and `section_end` read-only.
std::expected<FmIndex, Error> map_search_index(const std::filesystem::path& archive,
                                               uint64_t section_offset, uint64_t section_end,
                                               uint64_t total_size);
// Reads the whole section and its trailer and checks its CRC32, for verify.
std::expected<void, Error> check_search_index(std::istream& in, uint64_t section_offset,
                                              uint64_t section_end);

// About how many times `pattern` occurs (matches in the overlaps may count twice): backward search
// only, without locating anything.
std::expected<uint64_t, Error> fm_count(const FmIndex& index, std::string_view pattern,
                                        unsigned threads = 0);

// Where `pattern` (1 to fm_overlap bytes) starts in the data, in order; chunks are searched on
// `threads` threads (0: all cores). corrupt_index when a chunk's contents don't add up.
std::expected<std::vector<uint64_t>, Error> fm_find(const FmIndex& index, std::string_view pattern,
                                                    unsigned threads = 0);

} // namespace compressor

#endif /* FmIndex_h */
