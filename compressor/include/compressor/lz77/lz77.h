#ifndef Lz77_h
#define Lz77_h

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace compressor
{

inline constexpr std::size_t lz77_min_match = 3;
inline constexpr std::size_t lz77_max_match = 258;                 // Deflate's limits
inline constexpr std::size_t lz77_window = std::size_t{32} * 1024; // Deflate's farthest match

// A literal byte when `distance` is 0, otherwise a copy of `length` bytes from `distance` back.
struct Lz77Token
{
    uint16_t length_or_literal = 0;
    uint32_t distance = 0;

    bool operator==(const Lz77Token&) const = default;
};

// The defaults are zlib's level 6.
struct Lz77Options
{
    unsigned max_chain = 128;      // candidates tried per position
    std::size_t nice_length = 128; // stop looking once a match is this long
    bool lazy = true;              // emit a literal if the next position has a longer match
    std::size_t max_lazy = 16;     // only look at the next position for matches shorter than this
    std::size_t good_length = 8;   // and with a quarter of the chain from this length on
    std::size_t window = lz77_window; // the farthest a match may reach back
};

// 1 means no LZ77 at all (Huffman only); 2-9 grow from greedy and short chains to zlib's level 9.
inline constexpr unsigned min_level = 1;
inline constexpr unsigned max_level = 9;
Lz77Options lz77_options_for_level(unsigned level);

// Finds matches with hash chains over 3-byte prefixes. Input comes in pieces; matches may reach
// back into earlier pieces as far as the window.
class Lz77MatchFinder
{
public:
    explicit Lz77MatchFinder(Lz77Options options = {});

    // Appends input. Positions are counted from the first byte ever added.
    void add(std::span<const uint8_t> bytes);
    // Turns everything added so far into tokens, except up to lz77_max_match - 1 bytes kept back
    // for a match that may continue into the next piece, unless `final`.
    void tokenize(std::vector<Lz77Token>& tokens, bool final);
    // Total bytes turned into tokens.
    uint64_t position() const;
    // The bytes from `from` (not before position() - window, nor before what was dropped) up to
    // position().
    std::span<const uint8_t> history(uint64_t from) const;
    // Forgets bytes that no match can reach any more, keeping those from `keep_from` on.
    void drop_old(uint64_t keep_from);

private:
    std::size_t longest_match(uint64_t at, std::size_t limit, unsigned chain,
                              uint32_t& distance) const;
    void insert(uint64_t at);
    uint32_t hash_at(uint64_t at) const;
    const uint8_t* at_ptr(uint64_t at) const;

    Lz77Options options_;
    std::vector<uint8_t> data_;
    uint64_t base_ = 0;         // absolute position of data_[0]
    uint64_t position_ = 0;     // next byte to tokenize
    std::vector<int64_t> head_; // last position with each hash, or -1
    std::vector<int64_t>
        previous_; // the position before it with the same hash, by position % window
    // A match already found at position_ by the lazy look-ahead.
    bool have_next_ = false;
    std::size_t next_length_ = 0;
    uint32_t next_distance_ = 0;
};

} // namespace compressor

#endif /* Lz77_h */
