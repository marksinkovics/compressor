#ifndef Repair_h
#define Repair_h

#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <iosfwd>
#include <vector>

#include <compressor/error.h>

namespace compressor
{

// Repair data for any file, kept in a separate file (docs/file_format.md, "Repair file"): the file
// is cut into k equal shards (the last padded with zeros) and m parity shards are computed with a
// systematic Reed-Solomon code over GF(2^8) built from a Cauchy matrix, so any k intact shards of
// the k + m rebuild the others. Each shard's CRC32 finds the damaged ones.
inline constexpr unsigned default_repair_percent = 5;
inline constexpr unsigned max_shards = 255;

struct RepairLayout
{
    uint64_t file_size = 0;
    uint64_t shard_size = 0;
    unsigned data_shards = 0;   // k
    unsigned parity_shards = 0; // m
};

// k and m for a file: shards of at least 64 KiB, at most 200 data shards, and parity worth
// `percent` of them (at least 1).
RepairLayout repair_layout(uint64_t file_size, unsigned percent);

// Reads `file` (which must be able to seek) and writes its repair file to `out`.
std::expected<void, Error> write_repair(std::istream& file, uint64_t file_size, unsigned percent,
                                        std::ostream& out);

struct RepairCheck
{
    RepairLayout layout;
    std::vector<unsigned> damaged_data;   // shard numbers whose CRC32 doesn't match
    std::vector<unsigned> damaged_parity; // the same in the repair file
    bool size_matches = true;             // the file has the size the repair file records

    bool intact() const;
    // At least k shards are intact.
    bool recoverable() const;
};

// corrupt_header when the repair file's header is damaged or isn't one.
std::expected<RepairCheck, Error> check_repair(std::istream& file, std::istream& repair);

// Writes the original file to `out` from the damaged `file` and its repair file; corrupt_payload
// when too many shards are damaged.
std::expected<RepairCheck, Error> repair_file(std::istream& file, std::istream& repair,
                                              std::ostream& out);

} // namespace compressor

#endif /* Repair_h */
