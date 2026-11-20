#include <compressor/deflate/deflate.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <expected>
#include <future>
#include <istream>
#include <memory>
#include <ostream>
#include <span>
#include <string>
#include <vector>

#include <compressor/checksum/crc32.h>
#include <compressor/error.h>
#include <compressor/huffman/huffman.h>
#include <compressor/lz77/lz77.h>
#include <compressor/thread/pool.h>

namespace compressor
{

namespace
{

constexpr std::size_t literal_length_symbols = 286;
constexpr std::size_t distance_symbols = 30;
constexpr std::size_t code_length_symbols = 19;
constexpr uint16_t end_of_block = 256;
constexpr std::size_t max_code_length_code = 7;
constexpr std::size_t tokens_per_block = std::size_t{16} * 1024;
// Streams are deflated in pieces this large, in parallel when there is a thread pool.
constexpr std::size_t stream_piece_bytes = std::size_t{256} * 1024;
constexpr std::size_t max_stored_bytes = 65535;

constexpr std::array<uint16_t, 29> length_base{3,  4,  5,  6,   7,   8,   9,   10,  11, 13,
                                               15, 17, 19, 23,  27,  31,  35,  43,  51, 59,
                                               67, 83, 99, 115, 131, 163, 195, 227, 258};
constexpr std::array<uint8_t, 29> length_extra{0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
                                               2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
constexpr std::array<uint16_t, 30> distance_base{
    1,   2,   3,   4,   5,   7,    9,    13,   17,   25,   33,   49,   65,    97,    129,
    193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
constexpr std::array<uint8_t, 30> distance_extra{0, 0, 0,  0,  1,  1,  2,  2,  3,  3,
                                                 4, 4, 5,  5,  6,  6,  7,  7,  8,  8,
                                                 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};
constexpr std::array<uint8_t, code_length_symbols> code_length_order{
    16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};

// Index into length_base for a match length, and into distance_base for a distance.
constexpr std::array<uint8_t, 259> make_length_codes()
{
    std::array<uint8_t, 259> codes{};
    for (std::size_t code = 0; code < length_base.size(); ++code)
    {
        const std::size_t last = code + 1 < length_base.size() ? length_base[code + 1] : 259;
        for (std::size_t length = length_base[code]; length < last && length < 259; ++length)
        {
            codes[length] = static_cast<uint8_t>(code);
        }
    }
    codes[258] = 28;
    return codes;
}
constexpr std::array<uint8_t, 259> length_codes = make_length_codes();

// zlib's trick: distances up to 256 directly, larger ones by (distance - 1) >> 7.
constexpr std::array<uint8_t, 512> make_distance_codes()
{
    std::array<uint8_t, 512> codes{};
    for (std::size_t code = 0; code < distance_base.size(); ++code)
    {
        const std::size_t first = distance_base[code];
        const std::size_t last = first + (std::size_t{1} << distance_extra[code]);
        for (std::size_t distance = first; distance < last; ++distance)
        {
            const std::size_t index = distance <= 256 ? distance - 1 : 256 + ((distance - 1) >> 7U);
            codes[index] = static_cast<uint8_t>(code);
        }
    }
    return codes;
}
constexpr std::array<uint8_t, 512> distance_codes = make_distance_codes();

uint8_t distance_code(uint32_t distance)
{
    return distance <= 256 ? distance_codes[distance - 1U]
                           : distance_codes[256 + ((distance - 1U) >> 7U)];
}

// Deflate sends Huffman codes first bit first, and everything else least significant bit first.
class BitWriter
{
public:
    explicit BitWriter(std::vector<uint8_t>& out)
        : out_(out)
    {
    }

    void put_reversed(const Code& code)
    {
        put(code.bits, code.length);
    }

    // At most 32 bits at a time.
    void put(uint32_t bits, unsigned count)
    {
        buffer_ |= uint64_t{bits} << filled_;
        filled_ += count;
        if (filled_ >= 32)
        {
            const auto word = static_cast<uint32_t>(buffer_);
            out_.push_back(static_cast<uint8_t>(word));
            out_.push_back(static_cast<uint8_t>(word >> 8U));
            out_.push_back(static_cast<uint8_t>(word >> 16U));
            out_.push_back(static_cast<uint8_t>(word >> 24U));
            buffer_ >>= 32U;
            filled_ -= 32;
        }
    }

    void align()
    {
        if (filled_ % 8 != 0)
        {
            put(0, 8 - (filled_ % 8));
        }
        while (filled_ > 0)
        {
            out_.push_back(static_cast<uint8_t>(buffer_));
            buffer_ >>= 8U;
            filled_ -= 8;
        }
    }

private:
    std::vector<uint8_t>& out_;
    uint64_t buffer_ = 0;
    unsigned filled_ = 0;
};

// The codes bit-reversed, as Deflate sends a code's first bit first.
std::vector<Code> reversed_codes(std::span<const uint8_t> lengths)
{
    std::vector<Code> codes = canonical_codes(lengths);
    for (Code& code : codes)
    {
        uint32_t reversed = 0;
        for (unsigned bit = 0; bit < code.length; ++bit)
        {
            reversed |= ((code.bits >> bit) & 1U) << (code.length - 1U - bit);
        }
        code.bits = reversed;
    }
    return codes;
}

struct Tree
{
    std::vector<uint8_t> lengths;
    std::vector<Code> codes; // reversed
};

Tree make_tree(std::span<const uint64_t> frequencies, std::size_t max_length)
{
    Tree tree{.lengths = code_lengths(frequencies, max_length), .codes = {}};
    tree.codes = reversed_codes(tree.lengths);
    return tree;
}

Tree fixed_literal_tree()
{
    std::vector<uint8_t> lengths(288);
    std::fill(lengths.begin(), lengths.begin() + 144, 8);
    std::fill(lengths.begin() + 144, lengths.begin() + 256, 9);
    std::fill(lengths.begin() + 256, lengths.begin() + 280, 7);
    std::fill(lengths.begin() + 280, lengths.end(), 8);
    return {.lengths = lengths, .codes = reversed_codes(lengths)};
}

Tree fixed_distance_tree()
{
    const std::vector<uint8_t> lengths(30, 5);
    return {.lengths = lengths, .codes = reversed_codes(lengths)};
}

// The code length sequence run-length coded with symbols 16, 17 and 18 (RFC 1951, 3.2.7).
struct CodeLengthItem
{
    uint8_t symbol;
    uint8_t extra; // the repeat count minus its base
};

std::vector<CodeLengthItem> run_length_code(std::span<const uint8_t> lengths)
{
    std::vector<CodeLengthItem> items;
    for (std::size_t i = 0; i < lengths.size();)
    {
        const uint8_t length = lengths[i];
        std::size_t run = 1;
        while (i + run < lengths.size() && lengths[i + run] == length)
        {
            ++run;
        }
        std::size_t left = run;
        if (length == 0)
        {
            while (left >= 11)
            {
                const std::size_t take = std::min<std::size_t>(left, 138);
                items.push_back({.symbol = 18, .extra = static_cast<uint8_t>(take - 11)});
                left -= take;
            }
            if (left >= 3)
            {
                items.push_back({.symbol = 17, .extra = static_cast<uint8_t>(left - 3)});
                left = 0;
            }
        }
        else
        {
            items.push_back({.symbol = length, .extra = 0});
            --left;
            while (left >= 3)
            {
                const std::size_t take = std::min<std::size_t>(left, 6);
                items.push_back({.symbol = 16, .extra = static_cast<uint8_t>(take - 3)});
                left -= take;
            }
        }
        for (; left > 0; --left)
        {
            items.push_back({.symbol = length, .extra = 0});
        }
        i += run;
    }
    return items;
}

constexpr std::array<uint8_t, 19> code_length_extra_bits{0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                                                         0, 0, 0, 0, 0, 0, 2, 3, 7};

class BlockWriter
{
public:
    explicit BlockWriter(std::vector<uint8_t>& out)
        : bits_(out)
        , fixed_literals_(fixed_literal_tree())
        , fixed_distances_(fixed_distance_tree())
    {
    }

    // `raw`: the bytes the tokens stand for, for a stored block.
    void write(std::span<const Lz77Token> tokens, std::span<const uint8_t> raw, bool final)
    {
        std::vector<uint64_t> literal_frequencies(literal_length_symbols);
        std::vector<uint64_t> distance_frequencies(distance_symbols);
        for (const Lz77Token& token : tokens)
        {
            if (token.distance == 0)
            {
                ++literal_frequencies[token.length_or_literal];
            }
            else
            {
                ++literal_frequencies[257 + length_codes[token.length_or_literal]];
                ++distance_frequencies[distance_code(token.distance)];
            }
        }
        ++literal_frequencies[end_of_block];

        const Tree literals = make_tree(literal_frequencies, max_code_length);
        Tree distances = make_tree(distance_frequencies, max_code_length);
        if (std::ranges::all_of(distances.lengths, [](uint8_t length) {
            return length == 0;
        }))
        {
            // At least one distance code, as inflaters expect.
            distances.lengths[0] = 1;
            distances.codes = reversed_codes(distances.lengths);
        }

        const std::size_t literal_count = trimmed(literals.lengths, 257);
        const std::size_t distance_count = trimmed(distances.lengths, 1);
        std::vector<uint8_t> all_lengths(literals.lengths.begin(),
                                         literals.lengths.begin() +
                                             static_cast<std::ptrdiff_t>(literal_count));
        all_lengths.insert(all_lengths.end(), distances.lengths.begin(),
                           distances.lengths.begin() + static_cast<std::ptrdiff_t>(distance_count));
        const std::vector<CodeLengthItem> items = run_length_code(all_lengths);
        std::vector<uint64_t> item_frequencies(code_length_symbols);
        for (const CodeLengthItem& item : items)
        {
            ++item_frequencies[item.symbol];
        }
        const Tree code_lengths_tree = make_tree(item_frequencies, max_code_length_code);
        std::size_t code_length_count = code_length_symbols;
        while (code_length_count > 4 &&
               code_lengths_tree.lengths[code_length_order[code_length_count - 1]] == 0)
        {
            --code_length_count;
        }

        uint64_t dynamic_bits = 3 + 5 + 5 + 4 + (3 * code_length_count);
        for (const CodeLengthItem& item : items)
        {
            dynamic_bits +=
                code_lengths_tree.lengths[item.symbol] + code_length_extra_bits[item.symbol];
        }
        dynamic_bits += token_bits(tokens, literals, distances);
        const uint64_t fixed_bits = 3 + token_bits(tokens, fixed_literals_, fixed_distances_);
        const uint64_t stored_blocks =
            std::max<uint64_t>(1, (raw.size() + max_stored_bytes - 1) / max_stored_bytes);
        const uint64_t stored_bits = (stored_blocks * (3 + 7 + 32)) + (8 * raw.size());

        if (stored_bits <= fixed_bits && stored_bits <= dynamic_bits)
        {
            write_stored(raw, final);
        }
        else if (fixed_bits <= dynamic_bits)
        {
            bits_.put(final ? 1 : 0, 1);
            bits_.put(1, 2);
            write_tokens(tokens, fixed_literals_, fixed_distances_);
        }
        else
        {
            bits_.put(final ? 1 : 0, 1);
            bits_.put(2, 2);
            bits_.put(static_cast<uint32_t>(literal_count - 257), 5);
            bits_.put(static_cast<uint32_t>(distance_count - 1), 5);
            bits_.put(static_cast<uint32_t>(code_length_count - 4), 4);
            for (std::size_t i = 0; i < code_length_count; ++i)
            {
                bits_.put(code_lengths_tree.lengths[code_length_order[i]], 3);
            }
            for (const CodeLengthItem& item : items)
            {
                bits_.put_reversed(code_lengths_tree.codes[item.symbol]);
                bits_.put(item.extra, code_length_extra_bits[item.symbol]);
            }
            write_tokens(tokens, literals, distances);
        }
    }

    void finish()
    {
        bits_.align();
    }

    // An empty stored block, which ends on a byte boundary: zlib's Z_SYNC_FLUSH.
    void sync()
    {
        write_stored({}, false);
        bits_.align();
    }

private:
    static std::size_t trimmed(std::span<const uint8_t> lengths, std::size_t minimum)
    {
        std::size_t count = lengths.size();
        while (count > minimum && lengths[count - 1] == 0)
        {
            --count;
        }
        return count;
    }

    static uint64_t token_bits(std::span<const Lz77Token> tokens, const Tree& literals,
                               const Tree& distances)
    {
        uint64_t bits = literals.lengths[end_of_block];
        for (const Lz77Token& token : tokens)
        {
            if (token.distance == 0)
            {
                bits += literals.lengths[token.length_or_literal];
                continue;
            }
            const uint8_t length = length_codes[token.length_or_literal];
            const uint8_t distance = distance_code(token.distance);
            bits += literals.lengths[257 + length] + length_extra[length] +
                    distances.lengths[distance] + distance_extra[distance];
        }
        return bits;
    }

    void write_tokens(std::span<const Lz77Token> tokens, const Tree& literals,
                      const Tree& distances)
    {
        for (const Lz77Token& token : tokens)
        {
            if (token.distance == 0)
            {
                bits_.put_reversed(literals.codes[token.length_or_literal]);
                continue;
            }
            const uint8_t length = length_codes[token.length_or_literal];
            bits_.put_reversed(literals.codes[257 + length]);
            bits_.put(token.length_or_literal - length_base[length], length_extra[length]);
            const uint8_t distance = distance_code(token.distance);
            bits_.put_reversed(distances.codes[distance]);
            bits_.put(token.distance - distance_base[distance], distance_extra[distance]);
        }
        bits_.put_reversed(literals.codes[end_of_block]);
    }

    void write_stored(std::span<const uint8_t> raw, bool final)
    {
        do
        {
            const std::size_t take = std::min(raw.size(), max_stored_bytes);
            const bool last = final && take == raw.size();
            bits_.put(last ? 1 : 0, 1);
            bits_.put(0, 2);
            bits_.align();
            bits_.put(static_cast<uint32_t>(take), 16);
            bits_.put(static_cast<uint32_t>(~take & 0xFFFFU), 16);
            for (const uint8_t byte : raw.first(take))
            {
                bits_.put(byte, 8);
            }
            raw = raw.subspan(take);
        } while (!raw.empty());
    }

    BitWriter bits_;
    Tree fixed_literals_;
    Tree fixed_distances_;
};

void put_le(std::ostream& out, uint64_t value, std::size_t bytes)
{
    for (std::size_t i = 0; i < bytes; ++i)
    {
        out.put(static_cast<char>((value >> (8 * i)) & 0xFFU));
    }
}

enum class Flush : uint8_t
{
    none,  // more input follows
    sync,  // end on a byte boundary, the stream not ended
    final, // end the stream
};

// Raw Deflate over input added in pieces.
class DeflateWriter
{
public:
    explicit DeflateWriter(const Lz77Options& options)
        : finder_(options)
        , writer_(encoded_)
    {
    }

    void prime(std::span<const uint8_t> dictionary)
    {
        finder_.prime(dictionary);
        block_start_ = finder_.position();
    }

    // Appends what this piece completes to encoded().
    void add(std::span<const uint8_t> bytes, Flush flush)
    {
        const bool final = flush == Flush::final;
        finder_.add(bytes);
        finder_.tokenize(tokens_, flush != Flush::none);
        // Blocks of at most tokens_per_block, each with the raw bytes it covers.
        std::size_t first = 0;
        while (first < tokens_.size())
        {
            const std::size_t count = std::min(tokens_per_block, tokens_.size() - first);
            const std::span<const Lz77Token> block(tokens_.data() + first, count);
            uint64_t covered = 0;
            for (const Lz77Token& token : block)
            {
                covered += token.distance == 0 ? 1 : token.length_or_literal;
            }
            const bool last = final && first + count == tokens_.size();
            writer_.write(block, finder_.history(block_start_).first(covered), last);
            block_start_ += covered;
            first += count;
        }
        if (final && tokens_.empty())
        {
            writer_.write({}, {}, true);
        }
        if (final)
        {
            writer_.finish();
        }
        else if (flush == Flush::sync)
        {
            writer_.sync();
        }
        tokens_.clear();
        finder_.drop_old(block_start_);
    }

    std::vector<uint8_t>& encoded()
    {
        return encoded_;
    }

private:
    Lz77MatchFinder finder_;
    std::vector<Lz77Token> tokens_;
    std::vector<uint8_t> encoded_;
    BlockWriter writer_;
    uint64_t block_start_ = 0;
};

using Piece = std::shared_ptr<const std::vector<uint8_t>>;

// One piece of a stream: matches may reach into the end of the piece before it, and it ends on a
// byte boundary, so the pieces' outputs simply follow each other.
std::vector<uint8_t> deflate_piece(const Piece& previous, const Piece& piece, bool final,
                                   const Lz77Options& options)
{
    DeflateWriter writer(options);
    if (previous)
    {
        const std::size_t keep = std::min(previous->size(), options.window);
        writer.prime(std::span(*previous).last(keep));
    }
    writer.add(*piece, final ? Flush::final : Flush::sync);
    return std::move(writer.encoded());
}

} // namespace

std::vector<uint8_t> deflate_bytes(std::span<const uint8_t> bytes, const Lz77Options& options,
                                   std::span<const uint8_t> dictionary)
{
    DeflateWriter writer(options);
    if (!dictionary.empty())
    {
        writer.prime(dictionary.last(std::min(dictionary.size(), lz77_window)));
    }
    writer.add(bytes, Flush::final);
    return std::move(writer.encoded());
}

std::expected<DeflateResult, Error> deflate_stream(std::istream& in, std::ostream& out,
                                                   const Lz77Options& options, ThreadPool* pool)
{
    DeflateResult result;
    std::deque<std::future<std::vector<uint8_t>>> pending;
    const std::size_t in_flight = pool != nullptr ? 2 * pool->size() : 0;
    const auto write = [&](const std::vector<uint8_t>& encoded) {
        out.write(reinterpret_cast<const char*>(encoded.data()),
                  static_cast<std::streamsize>(encoded.size()));
        result.output_bytes += encoded.size();
        return static_cast<bool>(out);
    };
    Piece previous;
    bool done = false;
    while (!done)
    {
        auto piece = std::make_shared<std::vector<uint8_t>>(stream_piece_bytes);
        in.read(reinterpret_cast<char*>(piece->data()),
                static_cast<std::streamsize>(piece->size()));
        const auto got = static_cast<std::size_t>(in.gcount());
        done = got < piece->size() || in.peek() == std::istream::traits_type::eof();
        if (in.bad())
        {
            return std::unexpected(Error::read_failed);
        }
        piece->resize(got);
        result.crc = crc32(*piece, result.crc);
        result.input_bytes += got;

        Piece current = std::move(piece);
        if (pool == nullptr)
        {
            if (!write(deflate_piece(previous, current, done, options)))
            {
                return std::unexpected(Error::write_failed);
            }
        }
        else
        {
            pending.push_back(
                pool->submit([previous, current, done, options](std::size_t /*worker*/) {
                return deflate_piece(previous, current, done, options);
            }));
        }
        previous = std::move(current);
        while (!pending.empty() && (done || pending.size() >= in_flight))
        {
            const std::vector<uint8_t> encoded = pending.front().get();
            pending.pop_front();
            if (!write(encoded))
            {
                return std::unexpected(Error::write_failed);
            }
        }
    }
    return result;
}

std::expected<DeflateResult, Error> write_gzip(std::istream& in, std::ostream& out,
                                               const GzipHeader& header, const Lz77Options& options,
                                               ThreadPool* pool)
{
    constexpr uint8_t name_flag = 0x08;
    constexpr uint8_t unix_os = 3;
    out.put(static_cast<char>(0x1F));
    out.put(static_cast<char>(0x8B));
    out.put(8); // Deflate
    out.put(static_cast<char>(header.name.empty() ? 0 : name_flag));
    put_le(out, header.mtime, 4);
    out.put(0); // extra flags
    out.put(static_cast<char>(unix_os));
    if (!header.name.empty())
    {
        out.write(header.name.data(), static_cast<std::streamsize>(header.name.size()));
        out.put(0);
    }
    auto deflated = deflate_stream(in, out, options, pool);
    if (!deflated)
    {
        return deflated;
    }
    put_le(out, deflated->crc, 4);
    put_le(out, deflated->input_bytes & 0xFFFFFFFFU, 4);
    if (!out)
    {
        return std::unexpected(Error::write_failed);
    }
    return deflated;
}

} // namespace compressor
