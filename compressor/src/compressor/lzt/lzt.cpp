#include <compressor/lzt/lzt.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <span>
#include <utility>
#include <vector>

#include <compressor/entropy/tans.h>
#include <compressor/error.h>
#include <compressor/lz77/lz77.h>

namespace compressor
{

namespace
{

std::unexpected<Error> corrupt()
{
    return std::unexpected(Error::corrupt_payload);
}

struct Coded
{
    uint8_t code = 0;
    uint8_t extra_bits = 0;
    uint32_t extra = 0;
};

// Lengths: 0-15 as themselves, then two codes per power of two (its top two bits), the bits below
// as extra bits.
Coded length_code(uint32_t value)
{
    if (value < 16)
    {
        return {.code = static_cast<uint8_t>(value), .extra_bits = 0, .extra = 0};
    }
    const auto top = static_cast<unsigned>(std::bit_width(value)) - 1;
    return {.code = static_cast<uint8_t>(16 + (2 * (top - 4)) + ((value >> (top - 1)) & 1U)),
            .extra_bits = static_cast<uint8_t>(top - 1),
            .extra = value & ((uint32_t{1} << (top - 1)) - 1)};
}

unsigned length_extra_bits(uint8_t code)
{
    return code < 16 ? 0 : 4 + ((code - 16U) / 2) - 1;
}

uint32_t length_value(uint8_t code, uint32_t extra)
{
    if (code < 16)
    {
        return code;
    }
    const unsigned top = 4 + ((code - 16U) / 2);
    return ((2U | ((code - 16U) & 1U)) << (top - 1)) | extra;
}

constexpr uint8_t repeat_offset = 0;
constexpr unsigned max_offset_code = 32;
constexpr unsigned max_length_code = 16 + (2 * (32 - 4)); // codes for any 32-bit value

// Offsets (at least 1): code 1 + floor(log2(offset)), the bits below the top one as extra bits.
Coded offset_code(uint32_t offset)
{
    const auto top = static_cast<unsigned>(std::bit_width(std::max(offset, 1U))) - 1;
    return {.code = static_cast<uint8_t>(1 + top),
            .extra_bits = static_cast<uint8_t>(top),
            .extra = offset - (uint32_t{1} << top)};
}

// Coder 14's repeat offsets, most recent first, as zstd starts them.
constexpr std::size_t repeats = 3;
constexpr std::array<uint64_t, repeats> initial_repeats{1, 4, 8};

// Moves repeat `index` to the front, or with index == repeats pushes `offset` in front.
void use_offset(std::array<uint64_t, repeats>& recent, uint64_t offset, std::size_t index)
{
    const std::size_t from = std::min(index, repeats - 1);
    for (std::size_t k = from; k > 0; --k)
    {
        recent[k] = recent[k - 1];
    }
    recent[0] = offset;
}

class BitWriter
{
public:
    void put(uint32_t value, unsigned bits)
    {
        buffer_ |= uint64_t{value} << filled_;
        filled_ += bits;
        while (filled_ >= 8)
        {
            bytes_.push_back(static_cast<uint8_t>(buffer_));
            buffer_ >>= 8U;
            filled_ -= 8;
        }
    }

    std::vector<uint8_t> finish()
    {
        if (filled_ > 0)
        {
            bytes_.push_back(static_cast<uint8_t>(buffer_));
        }
        return std::move(bytes_);
    }

private:
    std::vector<uint8_t> bytes_;
    uint64_t buffer_ = 0;
    unsigned filled_ = 0;
};

class BitReader
{
public:
    explicit BitReader(std::span<const uint8_t> bytes)
        : bytes_(bytes)
    {
    }

    // False past the end.
    bool get(unsigned bits, uint32_t& value)
    {
        while (filled_ < bits)
        {
            if (next_ >= bytes_.size())
            {
                return false;
            }
            buffer_ |= uint64_t{bytes_[next_++]} << filled_;
            filled_ += 8;
        }
        value = static_cast<uint32_t>(buffer_ & ((uint64_t{1} << bits) - 1));
        buffer_ >>= bits;
        filled_ -= bits;
        return true;
    }

    // Every byte read and only zero padding left in the last one.
    bool done() const
    {
        return next_ == bytes_.size() && buffer_ == 0;
    }

private:
    std::span<const uint8_t> bytes_;
    std::size_t next_ = 0;
    uint64_t buffer_ = 0;
    unsigned filled_ = 0;
};

void put_varint(std::vector<uint8_t>& out, uint64_t value)
{
    while (value >= 0x80)
    {
        out.push_back(static_cast<uint8_t>(value | 0x80U));
        value >>= 7U;
    }
    out.push_back(static_cast<uint8_t>(value));
}

bool get_varint(std::span<const uint8_t> in, std::size_t& at, uint64_t& value)
{
    value = 0;
    for (unsigned shift = 0; shift < 35; shift += 7)
    {
        if (at >= in.size())
        {
            return false;
        }
        const uint8_t byte = in[at++];
        value |= uint64_t{byte & 0x7FU} << shift;
        if ((byte & 0x80U) == 0)
        {
            return true;
        }
    }
    return false;
}

// A stream's coded length, then the tANS stream (nothing for no symbols).
void put_stream(std::vector<uint8_t>& out, std::span<const uint8_t> symbols,
                TansTable table = TansTable::full)
{
    std::vector<uint8_t> coded;
    if (!symbols.empty())
    {
        tans_encode(symbols, coded, table);
    }
    put_varint(out, coded.size());
    out.insert(out.end(), coded.begin(), coded.end());
}

bool get_stream(std::span<const uint8_t> in, std::size_t& at, std::size_t count,
                std::vector<uint8_t>& symbols, TansTable table = TansTable::full)
{
    uint64_t length = 0;
    if (!get_varint(in, at, length) || length > in.size() - at || (count == 0) != (length == 0))
    {
        return false;
    }
    symbols.clear();
    if (count > 0 &&
        !tans_decode(in.subspan(at, static_cast<std::size_t>(length)), count, symbols, table))
    {
        return false;
    }
    at += static_cast<std::size_t>(length);
    return true;
}

} // namespace

std::vector<uint8_t> lzt_encode(std::span<const uint8_t> bytes, const Lz77Options& options)
{
    Lz77MatchFinder finder(options);
    finder.add(bytes);
    std::vector<Lz77Token> tokens;
    finder.tokenize(tokens, true);

    std::vector<uint8_t> literals;
    std::vector<uint8_t> literal_codes;
    std::vector<uint8_t> length_codes;
    std::vector<uint8_t> offset_codes;
    BitWriter extra;
    uint32_t run = 0;
    uint32_t last_offset = 0;
    for (const Lz77Token& token : tokens)
    {
        if (token.distance == 0)
        {
            literals.push_back(static_cast<uint8_t>(token.length_or_literal));
            ++run;
            continue;
        }
        const Coded literal = length_code(run);
        const Coded length = length_code(token.length_or_literal - lz77_min_match);
        const Coded offset = token.distance == last_offset ? Coded{.code = repeat_offset}
                                                           : offset_code(token.distance);
        literal_codes.push_back(literal.code);
        length_codes.push_back(length.code);
        offset_codes.push_back(offset.code);
        extra.put(literal.extra, literal.extra_bits);
        extra.put(length.extra, length.extra_bits);
        extra.put(offset.extra, offset.extra_bits);
        last_offset = token.distance;
        run = 0;
    }

    std::vector<uint8_t> out;
    put_varint(out, literal_codes.size());
    put_varint(out, literals.size());
    put_stream(out, literals);
    put_stream(out, literal_codes);
    put_stream(out, length_codes);
    put_stream(out, offset_codes);
    const std::vector<uint8_t> bits = extra.finish();
    out.insert(out.end(), bits.begin(), bits.end());
    return out;
}

namespace
{

// Prices are in 1/16 bit.
constexpr uint32_t bit = 16;
constexpr uint32_t unreachable = 0xFFFFFFFFU;
constexpr unsigned parse_hash_bits = 18;
constexpr std::size_t good_length = 24;

// What one sequence or literal costs, from how often each symbol was used: -log2 of its share,
// every symbol counted once more so unseen ones stay possible.
class Prices
{
public:
    Prices()
    {
        literal_.fill(8 * bit);
        for (std::size_t c = 0; c < code_count; ++c)
        {
            run_[c] = length_[c] = offset_[c] = 6 * bit;
        }
    }

    void learn(std::span<const uint8_t> literals, std::span<const uint8_t> runs,
               std::span<const uint8_t> lengths, std::span<const uint8_t> offsets)
    {
        fill(literal_, literals);
        fill(run_, runs);
        fill(length_, lengths);
        fill(offset_, offsets);
    }

    uint32_t literal(uint8_t byte) const
    {
        return literal_[byte];
    }

    uint32_t run(uint32_t value) const
    {
        const Coded coded = length_code(value);
        return run_[coded.code] + (coded.extra_bits * bit);
    }

    uint32_t length(uint32_t value) const
    {
        const Coded coded = length_code(value - lz77_min_match);
        return length_[coded.code] + (coded.extra_bits * bit);
    }

    uint32_t repeat(std::size_t index) const
    {
        return offset_[index];
    }

    uint32_t offset(uint32_t value) const
    {
        const auto top = static_cast<unsigned>(std::bit_width(value)) - 1;
        return offset_[repeats + top] + (top * bit);
    }

private:
    static constexpr std::size_t code_count = 256;

    template <std::size_t N>
    static void fill(std::array<uint32_t, N>& prices, std::span<const uint8_t> symbols)
    {
        std::array<uint64_t, N> counts{};
        counts.fill(1);
        for (const uint8_t symbol : symbols)
        {
            ++counts[symbol];
        }
        const auto total = static_cast<double>(symbols.size() + N);
        for (std::size_t i = 0; i < N; ++i)
        {
            prices[i] = static_cast<uint32_t>(std::log2(total / static_cast<double>(counts[i])) *
                                              static_cast<double>(bit));
        }
    }

    std::array<uint32_t, 256> literal_{};
    std::array<uint32_t, code_count> run_{};
    std::array<uint32_t, code_count> length_{};
    std::array<uint32_t, code_count> offset_{};
};

struct Sequence
{
    uint32_t run = 0;
    uint32_t length = 0;
    uint32_t offset = 0;
    std::size_t repeat = repeats; // the repeat used, or repeats for an explicit offset
};

struct Parsed
{
    std::vector<uint8_t> literals;
    std::vector<Sequence> sequences;
};

struct Node
{
    uint32_t price = unreachable;
    uint32_t from = 0;
    uint32_t length = 0; // 0: reached by a literal
    uint32_t offset = 0;
    uint32_t run = 0; // literals since the last match
    uint8_t repeat = repeats;
    std::array<uint64_t, repeats> recent = initial_repeats;
};

// Every match a hash chain over 4 bytes finds at a position, each longer than the one before.
class ChainFinder
{
public:
    ChainFinder(std::span<const uint8_t> bytes, unsigned depth)
        : bytes_(bytes)
        , depth_(depth)
        , head_(std::size_t{1} << parse_hash_bits, 0)
        , previous_(bytes.size(), 0)
    {
    }

    // `matches` gets (length, offset) pairs; stops at `nice`.
    void find(std::size_t at, std::size_t nice, std::vector<std::pair<uint32_t, uint32_t>>& matches)
    {
        matches.clear();
        if (at + 4 > bytes_.size())
        {
            return;
        }
        const std::size_t limit = bytes_.size() - at;
        uint32_t candidate = head_[hash(at)];
        std::size_t best = lz77_min_match - 1;
        unsigned steps = depth_;
        for (unsigned step = 0; step < steps && candidate != 0; ++step)
        {
            const std::size_t from = candidate - 1;
            const uint8_t* there = bytes_.data() + from;
            const uint8_t* here = bytes_.data() + at;
            if (there[best] == here[best])
            {
                const std::size_t length = common_length(there, here, limit);
                if (length > best)
                {
                    best = length;
                    matches.emplace_back(static_cast<uint32_t>(length),
                                         static_cast<uint32_t>(at - from));
                    if (length >= nice)
                    {
                        break;
                    }
                    // Like zlib's good_length: with a good match in hand, search a quarter as far.
                    if (length >= good_length)
                    {
                        steps = std::min(steps, step + 1 + (depth_ / 4));
                    }
                }
            }
            candidate = previous_[from];
        }
    }

    void insert(std::size_t at)
    {
        if (at + 4 <= bytes_.size())
        {
            const uint32_t h = hash(at);
            previous_[at] = head_[h];
            head_[h] = static_cast<uint32_t>(at + 1);
        }
    }

private:
    uint32_t hash(std::size_t at) const
    {
        uint32_t key = 0;
        std::memcpy(&key, bytes_.data() + at, 4);
        return (key * 0x9E3779B1U) >> (32U - parse_hash_bits);
    }

    std::span<const uint8_t> bytes_;
    unsigned depth_;
    std::vector<uint32_t> head_;     // position + 1 of the last 4 bytes with each hash, or 0
    std::vector<uint32_t> previous_; // position + 1 of the one before it, or 0
};

// The cheapest way through the block by `prices`: forward, every position tries a literal, the
// repeat offsets and every length of every match found, keeping per position the cheapest arrival
// and the repeat offsets it leaves; a match of `nice` or more is taken as it is.
Parsed parse(std::span<const uint8_t> bytes, const Prices& prices, unsigned depth, std::size_t nice)
{
    const std::size_t n = bytes.size();
    std::vector<Node> nodes(n + 1);
    nodes[0].price = 0;
    ChainFinder finder(bytes, depth);
    std::vector<std::pair<uint32_t, uint32_t>> matches;
    std::size_t skip_until = 0;
    const auto relax = [&](std::size_t from, std::size_t length, uint64_t offset,
                           std::size_t repeat, uint32_t price) {
        Node& to = nodes[from + length];
        if (price >= to.price)
        {
            return;
        }
        to.price = price;
        to.from = static_cast<uint32_t>(from);
        to.length = static_cast<uint32_t>(length);
        to.offset = static_cast<uint32_t>(offset);
        to.repeat = static_cast<uint8_t>(repeat);
        to.run = 0;
        to.recent = nodes[from].recent;
        use_offset(to.recent, offset, repeat);
    };
    for (std::size_t i = 0; i < n; ++i)
    {
        const Node& here = nodes[i];
        if (i < skip_until || here.price == unreachable)
        {
            finder.insert(i);
            continue;
        }
        // A literal.
        {
            const uint32_t price = here.price + prices.literal(bytes[i]);
            Node& to = nodes[i + 1];
            if (price < to.price)
            {
                to.price = price;
                to.from = static_cast<uint32_t>(i);
                to.length = 0;
                to.run = here.run + 1;
                to.recent = here.recent;
            }
        }
        const uint32_t before = here.price + prices.run(here.run);
        const std::size_t limit = n - i;
        std::size_t longest = 0;
        // The repeat offsets.
        for (std::size_t k = 0; k < repeats; ++k)
        {
            const uint64_t offset = here.recent[k];
            if (offset > i)
            {
                continue;
            }
            const std::size_t length =
                common_length(bytes.data() + i - offset, bytes.data() + i, limit);
            if (length < lz77_min_match)
            {
                continue;
            }
            const uint32_t base = before + prices.repeat(k);
            for (std::size_t l = lz77_min_match; l <= std::min(length, nice); ++l)
            {
                relax(i, l, offset, k, base + prices.length(static_cast<uint32_t>(l)));
            }
            if (length > nice)
            {
                relax(i, length, offset, k, base + prices.length(static_cast<uint32_t>(length)));
            }
            longest = std::max(longest, length);
        }
        // New matches.
        finder.find(i, nice, matches);
        std::size_t shorter = lz77_min_match - 1;
        for (const auto& [length, offset] : matches)
        {
            const uint32_t base = before + prices.offset(offset);
            for (std::size_t l = shorter + 1; l <= std::min<std::size_t>(length, nice); ++l)
            {
                relax(i, l, offset, repeats, base + prices.length(static_cast<uint32_t>(l)));
            }
            if (length > nice)
            {
                relax(i, length, offset, repeats, base + prices.length(length));
            }
            shorter = length;
            longest = std::max<std::size_t>(longest, length);
        }
        finder.insert(i);
        if (longest > nice)
        {
            skip_until = i + longest;
        }
    }

    // Back from the end, then forward into sequences.
    std::vector<std::size_t> path;
    for (std::size_t at = n; at > 0; at = nodes[at].from)
    {
        path.push_back(at);
    }
    Parsed parsed;
    uint32_t run = 0;
    for (std::size_t k = path.size(); k-- > 0;)
    {
        const Node& node = nodes[path[k]];
        if (node.length == 0)
        {
            parsed.literals.push_back(bytes[node.from]);
            ++run;
            continue;
        }
        parsed.sequences.push_back(
            {.run = run, .length = node.length, .offset = node.offset, .repeat = node.repeat});
        run = 0;
    }
    return parsed;
}

struct Streams
{
    std::vector<uint8_t> runs;
    std::vector<uint8_t> lengths;
    std::vector<uint8_t> offsets;
    std::vector<uint8_t> extra;
};

Streams code_sequences(std::span<const Sequence> sequences)
{
    Streams streams;
    BitWriter extra;
    for (const Sequence& sequence : sequences)
    {
        const Coded run = length_code(sequence.run);
        const Coded length = length_code(sequence.length - lz77_min_match);
        Coded offset{.code = static_cast<uint8_t>(sequence.repeat)};
        if (sequence.repeat == repeats)
        {
            offset = offset_code(sequence.offset);
            offset.code = static_cast<uint8_t>(offset.code - 1 + repeats);
        }
        streams.runs.push_back(run.code);
        streams.lengths.push_back(length.code);
        streams.offsets.push_back(offset.code);
        extra.put(run.extra, run.extra_bits);
        extra.put(length.extra, length.extra_bits);
        extra.put(offset.extra, offset.extra_bits);
    }
    streams.extra = extra.finish();
    return streams;
}

} // namespace

std::vector<uint8_t> lzt_optimal_encode(std::span<const uint8_t> bytes, unsigned passes,
                                        unsigned depth, std::size_t nice)
{
    // The first prices are rough guesses; each pass learns them from the parse before.
    Prices prices;
    Parsed parsed;
    for (unsigned pass = 0; pass < std::max(passes, 1U); ++pass)
    {
        parsed = parse(bytes, prices, depth, nice);
        const Streams streams = code_sequences(parsed.sequences);
        prices.learn(parsed.literals, streams.runs, streams.lengths, streams.offsets);
    }
    const Streams streams = code_sequences(parsed.sequences);
    std::vector<uint8_t> out;
    put_varint(out, parsed.sequences.size());
    put_varint(out, parsed.literals.size());
    put_stream(out, parsed.literals, TansTable::compact);
    put_stream(out, streams.runs, TansTable::compact);
    put_stream(out, streams.lengths, TansTable::compact);
    put_stream(out, streams.offsets, TansTable::compact);
    out.insert(out.end(), streams.extra.begin(), streams.extra.end());
    return out;
}

namespace
{

// Coder 13 (one repeat offset, offset codes 1 + log2) or 14 (three, codes 3 + log2).
std::expected<void, Error> decode_sequences(std::span<const uint8_t> payload,
                                            std::vector<uint8_t>& out, std::size_t size,
                                            bool three_repeats)
{
    std::size_t at = 0;
    uint64_t sequences = 0;
    uint64_t literal_count = 0;
    if (!get_varint(payload, at, sequences) || !get_varint(payload, at, literal_count) ||
        literal_count > size || sequences > size / lz77_min_match)
    {
        return corrupt();
    }
    std::vector<uint8_t> literals;
    std::vector<uint8_t> literal_codes;
    std::vector<uint8_t> length_codes;
    std::vector<uint8_t> offset_codes;
    const auto count = static_cast<std::size_t>(sequences);
    const TansTable table = three_repeats ? TansTable::compact : TansTable::full;
    if (!get_stream(payload, at, static_cast<std::size_t>(literal_count), literals, table) ||
        !get_stream(payload, at, count, literal_codes, table) ||
        !get_stream(payload, at, count, length_codes, table) ||
        !get_stream(payload, at, count, offset_codes, table))
    {
        return corrupt();
    }
    BitReader extra(payload.subspan(at));

    const std::size_t start = out.size();
    out.resize(start + size);
    uint8_t* const target = out.data() + start;
    std::size_t produced = 0;
    std::size_t literal_at = 0;
    uint32_t last_offset = 0;
    std::array<uint64_t, repeats> recent = initial_repeats;
    const unsigned first_offset_code = three_repeats ? repeats : 1;
    const auto fail = [&] {
        out.resize(start);
        return corrupt();
    };
    for (std::size_t i = 0; i < count; ++i)
    {
        uint32_t literal_extra = 0;
        uint32_t length_extra = 0;
        uint32_t offset_extra = 0;
        const uint8_t offset_code = offset_codes[i];
        const bool explicit_offset = offset_code >= first_offset_code;
        if (literal_codes[i] > max_length_code || length_codes[i] > max_length_code ||
            offset_code > max_offset_code + first_offset_code - 1 ||
            !extra.get(length_extra_bits(literal_codes[i]), literal_extra) ||
            !extra.get(length_extra_bits(length_codes[i]), length_extra) ||
            !extra.get(explicit_offset ? offset_code - first_offset_code : 0, offset_extra))
        {
            return fail();
        }
        const uint64_t run = length_value(literal_codes[i], literal_extra);
        const uint64_t length =
            uint64_t{length_value(length_codes[i], length_extra)} + lz77_min_match;
        uint64_t offset = last_offset;
        if (explicit_offset)
        {
            offset = (uint64_t{1} << (offset_code - first_offset_code)) | offset_extra;
        }
        if (three_repeats)
        {
            offset = explicit_offset ? offset : recent[offset_code];
            use_offset(recent, offset, explicit_offset ? repeats : offset_code);
        }
        if (run > literals.size() - literal_at || run + length > size - produced)
        {
            return fail();
        }
        std::copy_n(literals.begin() + static_cast<std::ptrdiff_t>(literal_at), run,
                    target + produced);
        literal_at += run;
        produced += run;
        if (offset == 0 || offset > produced)
        {
            return fail();
        }
        // Byte by byte: a match may overlap what it produces.
        for (std::size_t from = produced - offset, k = 0; k < length; ++k)
        {
            target[produced + k] = target[from + k];
        }
        produced += length;
        last_offset = static_cast<uint32_t>(offset);
    }
    const std::size_t rest = literals.size() - literal_at;
    if (rest != size - produced || !extra.done())
    {
        return fail();
    }
    std::copy_n(literals.begin() + static_cast<std::ptrdiff_t>(literal_at), rest,
                target + produced);
    return {};
}

} // namespace

std::expected<void, Error> lzt_decode(std::span<const uint8_t> payload, std::vector<uint8_t>& out,
                                      std::size_t size)
{
    return decode_sequences(payload, out, size, false);
}

std::expected<void, Error> lzt_optimal_decode(std::span<const uint8_t> payload,
                                              std::vector<uint8_t>& out, std::size_t size)
{
    return decode_sequences(payload, out, size, true);
}

} // namespace compressor
