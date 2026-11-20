#include <compressor/lzr/lzr.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <vector>

#include <compressor/error.h>
#include <compressor/lz77/lz77.h>

namespace compressor
{

namespace
{

// LZMA's constants.
constexpr unsigned probability_bits = 11;
constexpr uint16_t probability_half = 1U << (probability_bits - 1);
constexpr unsigned move_bits = 5;
constexpr uint32_t top = 1U << 24U;
constexpr unsigned states = 12;
constexpr unsigned position_bits = 2;
constexpr unsigned position_states = 1U << position_bits;
constexpr unsigned literal_context_bits = 3;
constexpr unsigned length_states = 4;
constexpr unsigned slot_bits = 6;
constexpr unsigned end_position_slot = 14; // slots from here use direct bits plus 4 aligned bits
constexpr unsigned aligned_bits = 4;
constexpr std::size_t min_match = 2;
constexpr std::size_t low_lengths = 8;
constexpr std::size_t mid_lengths = 8;
constexpr std::size_t high_lengths = 256;

using Probability = uint16_t;

unsigned after_literal(unsigned state)
{
    return state < 4 ? 0 : (state < 10 ? state - 3 : state - 6);
}

unsigned after_match(unsigned state)
{
    return state < 7 ? 7 : 10;
}

unsigned after_rep(unsigned state)
{
    return state < 7 ? 8 : 11;
}

unsigned position_slot(uint32_t distance)
{
    // distance is the real distance minus 1.
    if (distance < 4)
    {
        return distance;
    }
    const unsigned bits = 31U - static_cast<unsigned>(std::countl_zero(distance));
    return (bits * 2) + ((distance >> (bits - 1)) & 1U);
}

struct LengthModel
{
    Probability choice = probability_half;
    Probability choice2 = probability_half;
    std::array<std::array<Probability, low_lengths>, position_states> low{};
    std::array<std::array<Probability, mid_lengths>, position_states> mid{};
    std::array<Probability, high_lengths> high{};

    LengthModel()
    {
        for (auto& tree : low)
        {
            tree.fill(probability_half);
        }
        for (auto& tree : mid)
        {
            tree.fill(probability_half);
        }
        high.fill(probability_half);
    }
};

struct Model
{
    std::array<std::array<Probability, position_states>, states> is_match{};
    std::array<Probability, states> is_rep{};
    std::vector<Probability> literals;
    std::array<std::array<Probability, 1U << slot_bits>, length_states> slots{};
    std::array<Probability, 1U + 114> special{}; // reverse trees for slots 4-13
    std::array<Probability, 1U << aligned_bits> aligned{};
    LengthModel match_lengths;
    LengthModel rep_lengths;

    Model()
        : literals(std::size_t{0x300} << literal_context_bits, probability_half)
    {
        for (auto& row : is_match)
        {
            row.fill(probability_half);
        }
        is_rep.fill(probability_half);
        for (auto& tree : slots)
        {
            tree.fill(probability_half);
        }
        special.fill(probability_half);
        aligned.fill(probability_half);
    }
};

class RangeEncoder
{
public:
    explicit RangeEncoder(std::vector<uint8_t>& out)
        : out_(out)
    {
    }

    void bit(Probability& probability, unsigned value)
    {
        const uint32_t bound = (range_ >> probability_bits) * probability;
        if (value == 0)
        {
            range_ = bound;
            probability += ((1U << probability_bits) - probability) >> move_bits;
        }
        else
        {
            low_ += bound;
            range_ -= bound;
            probability -= probability >> move_bits;
        }
        normalise();
    }

    void direct(uint32_t value, unsigned count)
    {
        while (count-- > 0)
        {
            range_ >>= 1U;
            if (((value >> count) & 1U) != 0)
            {
                low_ += range_;
            }
            normalise();
        }
    }

    template <std::size_t N>
    void tree(std::array<Probability, N>& probabilities, unsigned bits, uint32_t value)
    {
        uint32_t node = 1;
        while (bits-- > 0)
        {
            const unsigned b = (value >> bits) & 1U;
            bit(probabilities[node], b);
            node = (node << 1U) | b;
        }
    }

    void reverse_tree(std::span<Probability> probabilities, unsigned bits, uint32_t value)
    {
        uint32_t node = 1;
        for (unsigned i = 0; i < bits; ++i)
        {
            const unsigned b = value & 1U;
            value >>= 1U;
            bit(probabilities[node], b);
            node = (node << 1U) | b;
        }
    }

    void finish()
    {
        for (int i = 0; i < 5; ++i)
        {
            shift_low();
        }
    }

private:
    void normalise()
    {
        while (range_ < top)
        {
            range_ <<= 8U;
            shift_low();
        }
    }

    void shift_low()
    {
        if (static_cast<uint32_t>(low_) < 0xFF000000U || (low_ >> 32U) != 0)
        {
            const auto carry = static_cast<uint8_t>(low_ >> 32U);
            uint8_t pending = cache_;
            do
            {
                out_.push_back(static_cast<uint8_t>(pending + carry));
                pending = 0xFF;
            } while (--cache_size_ != 0);
            cache_ = static_cast<uint8_t>(low_ >> 24U);
        }
        ++cache_size_;
        low_ = (low_ & 0x00FFFFFFU) << 8U;
    }

    std::vector<uint8_t>& out_;
    uint64_t low_ = 0;
    uint32_t range_ = 0xFFFFFFFFU;
    uint8_t cache_ = 0;
    uint64_t cache_size_ = 1;
};

class RangeDecoder
{
public:
    explicit RangeDecoder(std::span<const uint8_t> in)
        : in_(in)
    {
        for (int i = 0; i < 5; ++i)
        {
            code_ = (code_ << 8U) | next();
        }
    }

    unsigned bit(Probability& probability)
    {
        const uint32_t bound = (range_ >> probability_bits) * probability;
        unsigned value = 0;
        if (code_ < bound)
        {
            range_ = bound;
            probability += ((1U << probability_bits) - probability) >> move_bits;
        }
        else
        {
            code_ -= bound;
            range_ -= bound;
            probability -= probability >> move_bits;
            value = 1;
        }
        normalise();
        return value;
    }

    uint32_t direct(unsigned count)
    {
        uint32_t value = 0;
        while (count-- > 0)
        {
            range_ >>= 1U;
            const uint32_t b = code_ >= range_ ? 1U : 0U;
            code_ -= range_ & (0U - b);
            value = (value << 1U) | b;
            normalise();
        }
        return value;
    }

    template <std::size_t N> uint32_t tree(std::array<Probability, N>& probabilities, unsigned bits)
    {
        uint32_t node = 1;
        for (unsigned i = 0; i < bits; ++i)
        {
            node = (node << 1U) | bit(probabilities[node]);
        }
        return node - (1U << bits);
    }

    uint32_t reverse_tree(std::span<Probability> probabilities, unsigned bits)
    {
        uint32_t node = 1;
        uint32_t value = 0;
        for (unsigned i = 0; i < bits; ++i)
        {
            const unsigned b = bit(probabilities[node]);
            node = (node << 1U) | b;
            value |= b << i;
        }
        return value;
    }

    // The encoder's finish() wrote 5 bytes past what decoding needs, minus the 4 already read.
    bool clean() const
    {
        return overrun_ == 0 && next_ == in_.size();
    }

    bool overrun() const
    {
        return overrun_ > 0;
    }

private:
    uint8_t next()
    {
        if (next_ < in_.size())
        {
            return in_[next_++];
        }
        ++overrun_;
        return 0;
    }

    void normalise()
    {
        if (range_ < top)
        {
            range_ <<= 8U;
            code_ = (code_ << 8U) | next();
        }
    }

    std::span<const uint8_t> in_;
    std::size_t next_ = 0;
    std::size_t overrun_ = 0;
    uint32_t code_ = 0;
    uint32_t range_ = 0xFFFFFFFFU;
};

void encode_length(RangeEncoder& coder, LengthModel& model, std::size_t length, unsigned pos_state)
{
    std::size_t value = length - min_match;
    if (value < low_lengths)
    {
        coder.bit(model.choice, 0);
        coder.tree(model.low[pos_state], 3, static_cast<uint32_t>(value));
        return;
    }
    coder.bit(model.choice, 1);
    value -= low_lengths;
    if (value < mid_lengths)
    {
        coder.bit(model.choice2, 0);
        coder.tree(model.mid[pos_state], 3, static_cast<uint32_t>(value));
        return;
    }
    coder.bit(model.choice2, 1);
    coder.tree(model.high, 8, static_cast<uint32_t>(value - mid_lengths));
}

std::size_t decode_length(RangeDecoder& coder, LengthModel& model, unsigned pos_state)
{
    if (coder.bit(model.choice) == 0)
    {
        return min_match + coder.tree(model.low[pos_state], 3);
    }
    if (coder.bit(model.choice2) == 0)
    {
        return min_match + low_lengths + coder.tree(model.mid[pos_state], 3);
    }
    return min_match + low_lengths + mid_lengths + coder.tree(model.high, 8);
}

unsigned length_state(std::size_t length)
{
    return static_cast<unsigned>(std::min<std::size_t>(length - min_match, length_states - 1));
}

void encode_distance(RangeEncoder& coder, Model& model, uint32_t distance, std::size_t length)
{
    const uint32_t value = distance - 1;
    const unsigned slot = position_slot(value);
    coder.tree(model.slots[length_state(length)], slot_bits, slot);
    if (slot < 4)
    {
        return;
    }
    const unsigned footer = (slot >> 1U) - 1;
    const uint32_t base = (2U | (slot & 1U)) << footer;
    const uint32_t reduced = value - base;
    if (slot < end_position_slot)
    {
        coder.reverse_tree(std::span(model.special).subspan(base - slot), footer, reduced);
        return;
    }
    coder.direct(reduced >> aligned_bits, footer - aligned_bits);
    coder.reverse_tree(model.aligned, aligned_bits, reduced & ((1U << aligned_bits) - 1));
}

uint32_t decode_distance(RangeDecoder& coder, Model& model, std::size_t length)
{
    const unsigned slot = coder.tree(model.slots[length_state(length)], slot_bits);
    if (slot < 4)
    {
        return slot + 1;
    }
    const unsigned footer = (slot >> 1U) - 1;
    const uint32_t base = (2U | (slot & 1U)) << footer;
    if (slot < end_position_slot)
    {
        return base + coder.reverse_tree(std::span(model.special).subspan(base - slot), footer) + 1;
    }
    const uint32_t high = coder.direct(footer - aligned_bits) << aligned_bits;
    return base + high + coder.reverse_tree(model.aligned, aligned_bits) + 1;
}

std::size_t literal_base(uint8_t previous)
{
    return std::size_t{0x300} * (previous >> (8U - literal_context_bits));
}

} // namespace

std::vector<uint8_t> lzr_encode(std::span<const uint8_t> block, const Lz77Options& options,
                                std::span<const uint8_t> dictionary)
{
    // The model sees the dictionary and the block as one history; positions count from the block.
    std::vector<uint8_t> combined;
    std::span<const uint8_t> history = block;
    if (!dictionary.empty())
    {
        combined.assign(dictionary.begin(), dictionary.end());
        combined.insert(combined.end(), block.begin(), block.end());
        history = combined;
    }
    const std::size_t before = dictionary.size();
    Lz77Options window_options = options;
    window_options.window = std::max<std::size_t>(lz77_window, history.size());
    Lz77MatchFinder finder(window_options);
    if (!dictionary.empty())
    {
        finder.prime(dictionary);
    }
    finder.add(block);
    std::vector<Lz77Token> tokens;
    finder.tokenize(tokens, true);

    std::vector<uint8_t> out;
    RangeEncoder coder(out);
    Model model;
    unsigned state = 0;
    uint32_t last_distance = 1;
    std::size_t position = 0;
    for (const Lz77Token& token : tokens)
    {
        const unsigned pos_state = position & (position_states - 1);
        if (token.distance == 0)
        {
            coder.bit(model.is_match[state][pos_state], 0);
            const std::size_t at = before + position;
            const uint8_t previous = at > 0 ? history[at - 1] : 0;
            std::span<Probability> probabilities =
                std::span(model.literals).subspan(literal_base(previous), 0x300);
            const uint32_t symbol = token.length_or_literal;
            if (state >= 7 && at >= last_distance)
            {
                // A literal right after a match: the byte at the last distance predicts it.
                // Mirrors the decoder: while the bits so far agree with the match byte, its next
                // bit selects one of two probability sets; after the first difference, neither.
                uint32_t match_byte = history[at - last_distance];
                uint32_t offset = 0x100;
                uint32_t node = 1;
                for (unsigned i = 8; i-- > 0;)
                {
                    match_byte <<= 1U;
                    const uint32_t match_bit = match_byte & offset;
                    const unsigned b = (symbol >> i) & 1U;
                    coder.bit(probabilities[offset + match_bit + node], b);
                    node = (node << 1U) | b;
                    offset &= b != 0 ? match_bit : ~match_bit & 0x100U;
                }
            }
            else
            {
                uint32_t node = 1;
                for (unsigned i = 8; i-- > 0;)
                {
                    const unsigned b = (symbol >> i) & 1U;
                    coder.bit(probabilities[node], b);
                    node = (node << 1U) | b;
                }
            }
            state = after_literal(state);
            ++position;
            continue;
        }
        coder.bit(model.is_match[state][pos_state], 1);
        if (token.distance == last_distance)
        {
            coder.bit(model.is_rep[state], 1);
            encode_length(coder, model.rep_lengths, token.length_or_literal, pos_state);
            state = after_rep(state);
        }
        else
        {
            coder.bit(model.is_rep[state], 0);
            encode_length(coder, model.match_lengths, token.length_or_literal, pos_state);
            encode_distance(coder, model, token.distance, token.length_or_literal);
            last_distance = token.distance;
            state = after_match(state);
        }
        position += token.length_or_literal;
    }
    coder.finish();
    return out;
}

namespace
{

// Decodes into `out` after `before` bytes that matches may reach (a dictionary).
std::expected<void, Error> lzr_decode_after(std::span<const uint8_t> payload,
                                            std::vector<uint8_t>& out, std::size_t size,
                                            std::size_t before)
{
    const std::size_t start = out.size();
    const std::size_t end = start + size;
    out.reserve(end);
    RangeDecoder coder(payload);
    Model model;
    unsigned state = 0;
    uint32_t last_distance = 1;
    while (out.size() < end)
    {
        if (coder.overrun())
        {
            return std::unexpected(Error::corrupt_payload);
        }
        const std::size_t position = out.size() - start;
        const unsigned pos_state = position & (position_states - 1);
        if (coder.bit(model.is_match[state][pos_state]) == 0)
        {
            const uint8_t previous = position + before > 0 ? out.back() : 0;
            std::span<Probability> probabilities =
                std::span(model.literals).subspan(literal_base(previous), 0x300);
            uint32_t node = 1;
            if (state >= 7 && position + before >= last_distance)
            {
                uint32_t match_byte = out[out.size() - last_distance];
                uint32_t offset = 0x100;
                for (unsigned i = 0; i < 8; ++i)
                {
                    match_byte <<= 1U;
                    const uint32_t match_bit = match_byte & offset;
                    const unsigned b = coder.bit(probabilities[offset + match_bit + node]);
                    node = (node << 1U) | b;
                    offset &= b != 0 ? match_bit : ~match_bit & 0x100U;
                }
            }
            else
            {
                for (unsigned i = 0; i < 8; ++i)
                {
                    node = (node << 1U) | coder.bit(probabilities[node]);
                }
            }
            out.push_back(static_cast<uint8_t>(node));
            state = after_literal(state);
            continue;
        }
        std::size_t length = 0;
        if (coder.bit(model.is_rep[state]) == 1)
        {
            length = decode_length(coder, model.rep_lengths, pos_state);
            state = after_rep(state);
        }
        else
        {
            length = decode_length(coder, model.match_lengths, pos_state);
            last_distance = decode_distance(coder, model, length);
            state = after_match(state);
        }
        // Distance 0 is what a wrapped 32-bit distance decodes to.
        if (last_distance == 0 || last_distance > position + before || length > end - out.size())
        {
            return std::unexpected(Error::corrupt_payload);
        }
        for (std::size_t from = out.size() - last_distance, i = 0; i < length; ++i)
        {
            out.push_back(out[from + i]);
        }
    }
    if (!coder.clean())
    {
        return std::unexpected(Error::corrupt_payload);
    }
    return {};
}

} // namespace

std::expected<void, Error> lzr_decode(std::span<const uint8_t> payload, std::vector<uint8_t>& out,
                                      std::size_t size, std::span<const uint8_t> dictionary)
{
    if (dictionary.empty())
    {
        return lzr_decode_after(payload, out, size, 0);
    }
    std::vector<uint8_t> work(dictionary.begin(), dictionary.end());
    auto decoded = lzr_decode_after(payload, work, size, dictionary.size());
    if (decoded)
    {
        out.insert(out.end(), work.begin() + static_cast<std::ptrdiff_t>(dictionary.size()),
                   work.end());
    }
    return decoded;
}

} // namespace compressor
