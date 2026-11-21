#ifndef LongRange_h
#define LongRange_h

#include <cstddef>
#include <cstdint>
#include <deque>
#include <expected>
#include <functional>
#include <span>
#include <unordered_map>
#include <vector>

#include <compressor/data/data.h>
#include <compressor/engine/engine.h>
#include <compressor/error.h>

namespace compressor
{

// Long-range references (docs/file_format.md, "Long-range references"): with long_range_flag a
// block can copy parts of the literals of the blocks before it within the frame's window. A
// references block's literals are its own bytes that aren't copied; any other block's literals are
// all its bytes. Copies never point at copies, so a block needs only its targets' literals.
struct Reference
{
    uint32_t literals = 0; // the block's own literal bytes before the copy
    uint32_t back = 0;     // the target block, counted back from this one (at least 1)
    uint32_t offset = 0;   // into the target's literals
    uint32_t length = 0;

    bool operator==(const Reference&) const = default;
};

// A references block's payload: the references, then its literals as a block of their own.
std::vector<uint8_t> write_references(std::span<const Reference> references,
                                      const EncodedBlock& literals);
std::expected<void, Error> read_references(std::span<const uint8_t> payload,
                                           std::vector<Reference>& references,
                                           EncodedBlock& literals);

// The literals of the block `back` blocks before the one being rebuilt.
using EarlierLiterals = std::function<std::expected<std::span<const uint8_t>, Error>(uint32_t)>;

// Appends a references block's bytes to `out` from its references and decoded literals, checking
// its size and CRC32.
std::expected<void, Error> rebuild_block(const EncodedBlock& block,
                                         std::span<const Reference> references,
                                         std::span<const uint8_t> literals,
                                         const EarlierLiterals& earlier, std::vector<uint8_t>& out);

// Splits blocks, in order, into references to earlier blocks' literals and their own literals.
// Repeats are found by chunk (chunk_ends()), checked byte for byte and extended both ways. Holds
// the literals of the blocks in the window.
class LongRangeMatcher
{
public:
    explicit LongRangeMatcher(uint64_t window_blocks);

    struct Split
    {
        std::vector<Reference> references; // empty: the block is all literals
        std::vector<uint8_t> literals;     // only with references
    };
    Split next(std::span<const uint8_t> block);

private:
    struct Location
    {
        uint64_t block = 0;
        uint32_t offset = 0;
        uint32_t length = 0;
    };

    void remember(std::span<const uint8_t> literals);

    uint64_t window_blocks_;
    uint64_t blocks_ = 0; // blocks seen; the next one's index
    std::deque<std::vector<uint8_t>> literals_;
    std::deque<std::vector<uint64_t>> hashes_; // each remembered block's chunk hashes
    std::unordered_map<uint64_t, Location> index_;
};

// The literals of the blocks in the window, for decoding a long-range frame's blocks in order.
class LongRangeHistory
{
public:
    explicit LongRangeHistory(uint64_t window_blocks);

    // The literals of the block `back` blocks before the next one.
    std::expected<std::span<const uint8_t>, Error> earlier(uint32_t back) const;
    // Then the next block.
    void add(std::vector<uint8_t> literals);
    // Decodes the next block of any method with `engine`, appending its bytes to `out`.
    std::expected<void, Error> decode(IEngine& engine, const EncodedBlock& block,
                                      std::vector<uint8_t>& out);

private:
    uint64_t window_blocks_;
    std::deque<std::vector<uint8_t>> literals_;
};

uint64_t window_blocks(uint8_t block_size_log2, uint8_t window_log2);

} // namespace compressor

#endif /* LongRange_h */
