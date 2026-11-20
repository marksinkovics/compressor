#ifndef Stats_h
#define Stats_h

#include <cstdint>
#include <expected>
#include <iosfwd>
#include <optional>
#include <vector>

#include <compressor/data/data.h>
#include <compressor/error.h>
#include <compressor/huffman/huffman.h>

namespace compressor
{

struct BlockStats
{
    Method method = Method::stored;
    uint32_t size = 0;          // decoded bytes
    uint64_t encoded_bytes = 0; // header included
};

struct ArchiveCounts
{
    uint64_t files = 0;
    uint64_t directories = 0;
    uint64_t symlinks = 0;
};

struct Stats
{
    uint64_t input_bytes = 0;
    bool ingot = false; // otherwise the content is the input itself
    uint8_t block_size_log2 = 0;
    std::vector<BlockStats> blocks;
    std::optional<ArchiveCounts> archive;
    uint64_t content_bytes = 0; // decoded
    Frequencies frequencies{};  // of the content
};

// Reads all of `in`, which must be able to seek. An ingot file is decoded block by block, and its
// blocks and any archive index are checked as when decoding.
std::expected<Stats, Error> collect_stats(std::istream& in);

// Order-0 entropy in bits per byte; 0 for no content.
double entropy_bits_per_byte(const Frequencies& frequencies);

// Bits for the content with one length-limited Huffman code over all of it, code table excluded.
uint64_t huffman_bits(const Frequencies& frequencies);

struct StatsReport
{
    bool blocks = false;    // a line per block
    bool histogram = false; // a line per byte value present, most frequent first
};

void print_stats(const Stats& stats, const StatsReport& report, std::ostream& out);

// write_code_graph() of one Huffman code over the whole content.
void write_content_graph(const Stats& stats, std::ostream& out);

} // namespace compressor

#endif /* Stats_h */
