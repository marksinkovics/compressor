#ifndef ArchiveIndex_h
#define ArchiveIndex_h

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <iosfwd>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <compressor/error.h>

namespace compressor
{

// docs/file_format.md, "Archive". The values are stored in the file.
enum class EntryType : uint8_t
{
    file = 0,
    directory = 1,
    symlink = 2,
};

struct Entry
{
    EntryType type = EntryType::file;
    std::string path;     // relative, '/'-separated
    uint16_t mode = 0;    // permission bits, at most 07777
    int64_t mtime_ns = 0; // since the Unix epoch
    // Files only: where the bytes are in the decoded data, and their CRC32.
    uint64_t offset = 0;
    uint64_t size = 0;
    uint32_t crc = 0;
    std::string target; // symlinks only

    bool operator==(const Entry&) const = default;
};

struct ArchiveIndex
{
    std::vector<uint64_t> block_offsets; // of each block header, from the start of the file
    std::vector<Entry> entries;

    bool operator==(const ArchiveIndex&) const = default;
};

inline constexpr std::array<uint8_t, 4> index_magic{0x89, 'I', 'D', 'X'};
inline constexpr std::size_t trailer_bytes = 16;
inline constexpr std::size_t max_name_bytes = 0xFFFF;
inline constexpr uint16_t max_mode = 07777;

// The index and the trailer; `index_offset` is where the index starts in the file. Entries must
// already pass validate_index().
void write_index(std::ostream& ostream, const ArchiveIndex& index, uint64_t index_offset);

struct ArchiveInfo
{
    uint8_t block_size_log2 = 0;
    uint64_t total_size = 0;   // decoded bytes, from the end block
    uint64_t index_offset = 0; // just after the end block
    ArchiveIndex index;
    // Where the search index section starts and ends (its trailer follows), if there is one.
    std::optional<std::pair<uint64_t, uint64_t>> search_index;
    uint16_t flags = 0;
    uint8_t window_log2 = 0; // with long_range_flag
};

// Seeks around a whole archive file to read its frame header, end block and index, and checks
// every index rule that needs no decoding.
std::expected<ArchiveInfo, Error> read_archive_index(std::istream& istream);

// The index rules; `end_offset` is where the end block starts, `flags` the frame header's.
std::expected<void, Error> validate_index(const ArchiveIndex& index, uint8_t block_size_log2,
                                          uint64_t total_size, uint64_t end_offset,
                                          uint16_t flags = 0);

} // namespace compressor

#endif /* ArchiveIndex_h */
