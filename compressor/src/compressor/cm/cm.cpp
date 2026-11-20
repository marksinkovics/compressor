#include <compressor/cm/cm.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <vector>

#include <compressor/bwt/bwt.h>
#include <compressor/error.h>

namespace compressor
{

namespace
{

// Probabilities are 12 bits (of a 1); the logistic domain ("stretch") is -2047..2047, 8
// fractional bits.
int squash_exact(int x)
{
    const double v = 4096.0 / (1.0 + std::exp(-x / 256.0));
    return std::clamp(static_cast<int>(v), 1, 4095);
}

struct Tables
{
    std::array<int16_t, 4096> stretch{};
    std::array<int16_t, 4096> squash{}; // by x + 2048
    std::array<int, 1024> reciprocal{}; // 65536 / (n + 2)

    Tables()
    {
        for (std::size_t i = 0; i < squash.size(); ++i)
        {
            squash[i] = static_cast<int16_t>(squash_exact(static_cast<int>(i) - 2048));
        }
        int previous = 0;
        for (int x = -2047; x <= 2047; ++x)
        {
            const int v = squash_exact(x);
            for (int p = previous; p <= v; ++p)
            {
                stretch[static_cast<std::size_t>(p)] = static_cast<int16_t>(x);
            }
            previous = v + 1;
        }
        for (int p = previous; p < 4096; ++p)
        {
            stretch[static_cast<std::size_t>(p)] = 2047;
        }
        for (std::size_t n = 0; n < reciprocal.size(); ++n)
        {
            reciprocal[n] = static_cast<int>(65536.0 / (static_cast<double>(n) + 2.0));
        }
    }
};

const Tables& tables()
{
    static const Tables built;
    return built;
}

int squash(int x)
{
    const int index = std::clamp(x, -2047, 2047) + 2048;
    return tables().squash[static_cast<std::size_t>(index)];
}

int stretch(int p)
{
    return tables().stretch[static_cast<std::size_t>(p)];
}

// v >> shift for a negative v too (rounding down), for |v| < 2^62. Without a branch: the sign
// of v is unpredictable and a branch on it cost most of the coders' time.
int64_t floor_shift(int64_t v, unsigned shift)
{
    constexpr uint64_t bias = uint64_t{1} << 62U;
    return static_cast<int64_t>(((static_cast<uint64_t>(v) + bias) >> shift) - (bias >> shift));
}

// An adaptive probability: 22 bits of it and a 10-bit count of updates, which sets the rate
// (1 / (count + 2)) until it reaches the limit.
struct Counter
{
    static constexpr uint32_t initial = uint32_t{1} << 31U;

    static int p(uint32_t state)
    {
        return static_cast<int>(state >> 20U);
    }

    static void update(uint32_t& state, unsigned bit, uint32_t limit)
    {
        const uint32_t count = state & 1023U;
        const auto probability = static_cast<int64_t>(state >> 10U);
        const int64_t target = bit != 0 ? int64_t{(1U << 22U) - 1U} : 0;
        const int64_t moved =
            probability + floor_shift((target - probability) * tables().reciprocal[count], 16);
        state = (static_cast<uint32_t>(moved) << 10U) | (count < limit ? count + 1 : count);
    }
};

// Maps a probability and a small context to a refined probability: 33 buckets over the stretched
// input, interpolated.
class Apm
{
public:
    explicit Apm(std::size_t contexts)
        : table_(contexts * 33)
    {
        for (std::size_t c = 0; c < contexts; ++c)
        {
            for (std::size_t j = 0; j < 33; ++j)
            {
                table_[(c * 33) + j] =
                    static_cast<uint16_t>(squash((static_cast<int>(j) - 16) * 128) * 16);
            }
        }
    }

    int p(int probability, std::size_t context)
    {
        const auto s = static_cast<unsigned>(stretch(probability) + 2048);
        const unsigned weight = s & 127U;
        index_ = (context * 33) + (s >> 7U);
        weight_ = static_cast<int>(weight);
        return static_cast<int>(((unsigned{table_[index_]} * (128 - weight)) +
                                 (unsigned{table_[index_ + 1]} * weight)) >>
                                11U);
    }

    void update(unsigned bit)
    {
        const int target = bit != 0 ? 65535 : 0;
        const auto move = [&](uint16_t& entry, int share) {
            entry = static_cast<uint16_t>(entry + ((target - entry) * share / (128 * rate)));
        };
        move(table_[index_], 128 - weight_);
        move(table_[index_ + 1], weight_);
    }

private:
    static constexpr int rate = 48;
    std::vector<uint16_t> table_;
    std::size_t index_ = 0;
    int weight_ = 0;
};

uint64_t mix64(uint64_t x)
{
    x ^= x >> 31U;
    x *= 0x7FB5D329728EA185ULL;
    x ^= x >> 27U;
    x *= 0x81DADEF4BC2DD44DULL;
    x ^= x >> 33U;
    return x;
}

// Hashed contexts (orders 2, 3, 4 and 6 and the current word) share one table of 64-byte lines:
// a check word, then the counters of the 15 bit positions of one nibble.
constexpr std::size_t hashed_contexts = 5;
constexpr std::size_t inputs = 3 + hashed_contexts + 1; // order 0, order 1, hashed, match, bias
constexpr std::size_t line_words = 16;
constexpr std::size_t match_min = 6;
constexpr std::size_t match_hash_bits = 18;
constexpr std::size_t match_max = 65535;

// A logistic mixer: weight sets selected by a context, each trained on its own output's error.
class Mixer
{
public:
    Mixer(std::size_t sets, std::size_t inputs)
        : inputs_(inputs)
        , weights_(sets * inputs, 16384)
    {
        for (std::size_t set = 0; set < sets; ++set)
        {
            weights_[(set * inputs) + inputs - 1] = 0;
        }
    }

    // The mixed prediction in the logistic domain.
    int mix(std::span<const int> inputs, std::size_t set)
    {
        set_ = set * inputs_;
        int64_t dot = 0;
        for (std::size_t i = 0; i < inputs_; ++i)
        {
            dot += int64_t{inputs[i]} * weights_[set_ + i];
        }
        const int stretched =
            static_cast<int>(std::clamp<int64_t>(floor_shift(dot, 16), -2047, 2047));
        p_ = squash(stretched);
        return stretched;
    }

    void update(std::span<const int> inputs, unsigned bit, int rate)
    {
        const int error = (static_cast<int>(bit * 4096U) - p_) * rate;
        for (std::size_t i = 0; i < inputs_; ++i)
        {
            weights_[set_ + i] += static_cast<int>(floor_shift(int64_t{inputs[i]} * error, 10));
        }
    }

private:
    std::size_t inputs_;
    std::vector<int> weights_;
    std::size_t set_ = 0;
    int p_ = 2048;
};

class Model
{
public:
    Model(std::span<const uint8_t> history, unsigned table_log2)
        : history_(history)
        , line_bits_(table_log2 - 6)
        , lines_(std::size_t{line_words} << line_bits_, Counter::initial)
        , order0_(std::size_t{256}, Counter::initial)
        , order1_(std::size_t{256} * 256, Counter::initial)
        , match_counters_(std::size_t{64} * 2, Counter::initial)
        , match_table_(std::size_t{1} << match_hash_bits, 0)
        , by_partial_(std::size_t{256} * 4, inputs)
        , by_last_byte_(std::size_t{256}, inputs)
        , apm_order0_(256)
        , apm_order1_(std::size_t{256} * 256)
    {
        start_byte(0);
    }

    // Probability that the next bit is a 1, 12 bits.
    int p()
    {
        const std::size_t node = nibble_;
        inputs_[0] = stretch(Counter::p(order0_[partial_]));
        inputs_[1] = stretch(Counter::p(order1_[(std::size_t{last_byte_} << 8U) | partial_]));
        for (std::size_t i = 0; i < hashed_contexts; ++i)
        {
            inputs_[2 + i] = stretch(Counter::p(lines_[line_[i] + node]));
        }
        expected_bit_ = 2;
        if (match_length_ > 0)
        {
            const unsigned expected = unsigned{history_[match_ptr_]} | 256U;
            const unsigned shift = 8 - bit_;
            if ((expected >> shift) == partial_)
            {
                expected_bit_ = (expected >> (shift - 1)) & 1U;
            }
            else
            {
                match_length_ = 0;
            }
        }
        if (expected_bit_ < 2)
        {
            match_slot_ = (std::min<std::size_t>(match_length_, 31) * 2) + expected_bit_;
            const int s = stretch(Counter::p(match_counters_[match_slot_]));
            inputs_[2 + hashed_contexts] = s;
        }
        else
        {
            inputs_[2 + hashed_contexts] = 0;
        }
        inputs_[inputs - 1] = 256;

        const std::size_t bucket = match_length_ == 0   ? 0
                                   : match_length_ < 16 ? 1
                                   : match_length_ < 32 ? 2
                                                        : 3;
        const int first = by_partial_.mix(inputs_, (bucket << 8U) | partial_);
        const int second = by_last_byte_.mix(inputs_, last_byte_);
        mixed_ = squash((first + second) / 2);
        const int refined0 = apm_order0_.p(mixed_, partial_);
        const int refined1 = apm_order1_.p(mixed_, (std::size_t{last_byte_} << 8U) | partial_);
        const int p = (mixed_ + refined0 + (2 * refined1) + 2) / 4;
        return std::clamp(p, 1, 4095);
    }

    void update(unsigned bit)
    {
        by_partial_.update(inputs_, bit, 2);
        by_last_byte_.update(inputs_, bit, 2);
        apm_order0_.update(bit);
        apm_order1_.update(bit);
        Counter::update(order0_[partial_], bit, 60);
        Counter::update(order1_[(std::size_t{last_byte_} << 8U) | partial_], bit, 60);
        for (std::size_t i = 0; i < hashed_contexts; ++i)
        {
            Counter::update(lines_[line_[i] + nibble_], bit, 20);
        }
        if (expected_bit_ < 2)
        {
            Counter::update(match_counters_[match_slot_], bit, 1023);
        }

        partial_ = (partial_ << 1U) | bit;
        nibble_ = (nibble_ << 1U) | bit;
        ++bit_;
        if (bit_ == 8)
        {
            start_byte(static_cast<uint8_t>(partial_ & 0xFFU));
        }
        else if (bit_ == 4)
        {
            find_lines();
        }
    }

private:
    void start_byte(uint8_t byte)
    {
        if (bit_ == 8)
        {
            ++position_;
            update_match(byte);
            last_bytes_ = (last_bytes_ << 8U) | byte;
            last_byte_ = byte;
            const bool letter =
                (byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z') || byte >= 128;
            word_ = letter ? mix64(word_ + (byte | 32U) + 1) : 0;
        }
        bit_ = 0;
        partial_ = 1;
        const std::array<uint64_t, hashed_contexts> contexts{
            last_bytes_ & 0xFFFFU, last_bytes_ & 0xFFFFFFU, last_bytes_ & 0xFFFFFFFFU,
            last_bytes_ & 0xFFFFFFFFFFFFU, word_ ^ (last_bytes_ & 0xFFU)};
        for (std::size_t i = 0; i < hashed_contexts; ++i)
        {
            context_[i] = mix64(contexts[i] + ((i + 1) << 56U));
        }
        find_lines();
    }

    void update_match(uint8_t byte)
    {
        if (match_length_ > 0 && history_[match_ptr_] == byte)
        {
            match_length_ = std::min(match_length_ + 1, match_max);
            ++match_ptr_;
        }
        else
        {
            match_length_ = 0;
        }
        if (position_ < match_min)
        {
            return;
        }
        uint64_t h = 0;
        for (std::size_t i = position_ - match_min; i < position_; ++i)
        {
            h = (h << 8U) | history_[i];
        }
        uint32_t& slot = match_table_[mix64(h) >> (64U - match_hash_bits)];
        if (match_length_ == 0 && slot > 0)
        {
            std::size_t length = 0;
            while (length < slot && length < 32 &&
                   history_[slot - 1 - length] == history_[position_ - 1 - length])
            {
                ++length;
            }
            if (length >= match_min)
            {
                match_length_ = length;
                match_ptr_ = slot;
            }
        }
        slot = static_cast<uint32_t>(position_);
    }

    // The line for each hashed context and the current nibble: of two neighbouring lines, the
    // one with this check word, or else the less used one, cleared.
    void find_lines()
    {
        nibble_ = 1;
        for (std::size_t i = 0; i < hashed_contexts; ++i)
        {
            const uint64_t h = mix64(context_[i] + partial_);
            const auto check = static_cast<uint32_t>(h);
            const std::size_t first =
                static_cast<std::size_t>(h >> (64U - line_bits_)) & ~std::size_t{1};
            std::size_t chosen = first * line_words;
            const std::size_t other = (first + 1) * line_words;
            if (lines_[chosen] != check)
            {
                if (lines_[other] == check)
                {
                    chosen = other;
                }
                else
                {
                    if ((lines_[other + 1] & 1023U) < (lines_[chosen + 1] & 1023U))
                    {
                        chosen = other;
                    }
                    lines_[chosen] = check;
                    std::fill_n(lines_.begin() + static_cast<std::ptrdiff_t>(chosen + 1),
                                line_words - 1, Counter::initial);
                }
            }
            line_[i] = chosen;
        }
    }

    std::span<const uint8_t> history_;
    std::size_t position_ = 0; // bytes coded
    unsigned line_bits_;
    std::vector<uint32_t> lines_;
    std::vector<uint32_t> order0_;
    std::vector<uint32_t> order1_;
    std::vector<uint32_t> match_counters_;
    std::vector<uint32_t> match_table_;
    Mixer by_partial_;
    Mixer by_last_byte_;
    Apm apm_order0_;
    Apm apm_order1_;

    std::array<uint64_t, hashed_contexts> context_{};
    std::array<std::size_t, hashed_contexts> line_{};
    std::array<int, inputs> inputs_{};
    uint64_t last_bytes_ = 0;
    uint64_t word_ = 0;
    uint8_t last_byte_ = 0;
    unsigned partial_ = 1; // the bits of the current byte after a leading 1
    std::size_t nibble_ = 1;
    unsigned bit_ = 0;
    std::size_t match_ptr_ = 0;
    std::size_t match_length_ = 0;
    unsigned expected_bit_ = 2; // 2: no prediction
    std::size_t match_slot_ = 0;
    int mixed_ = 2048;
};

// For BWT output: long runs of a byte, and contexts that change slowly. Order 1 at two rates,
// hashed order 2, and the current run of the previous byte, mixed by weights selected by the run
// and the partial byte, then refined by an APM over the same.
class BwtModel
{
public:
    BwtModel()
        : order1_(std::size_t{65536} * 2, Counter::initial)
        , order2_(std::size_t{1} << order2_bits, Counter::initial)
        , run_counters_(std::size_t{64} * 2, Counter::initial)
        , mixer_(std::size_t{4} * 256, bwt_inputs)
        , apm_(std::size_t{4} * 256)
    {
    }

    int p()
    {
        i1_ = ((std::size_t{c1_} << 8U) | partial_) * 2;
        o2_ = h2_ + partial_;
        inputs_[0] = stretch(Counter::p(order1_[i1_]));
        inputs_[1] = stretch(Counter::p(order1_[i1_ + 1]));
        inputs_[2] = stretch(Counter::p(order2_[o2_]));
        expected_bit_ = 2;
        const unsigned expected = unsigned{c1_} | 256U;
        const unsigned shift = 8 - bit_;
        if ((expected >> shift) == partial_)
        {
            expected_bit_ = (expected >> (shift - 1)) & 1U;
            run_slot_ = (std::min<std::size_t>(run_, 63) * 2) + expected_bit_;
            inputs_[3] = stretch(Counter::p(run_counters_[run_slot_]));
        }
        else
        {
            inputs_[3] = 0;
        }
        inputs_[4] = 256;
        const std::size_t bucket = expected_bit_ == 2 ? 0 : run_ < 4 ? 1 : run_ < 16 ? 2 : 3;
        const std::size_t set = (bucket << 8U) | partial_;
        const int mixed = squash(mixer_.mix(inputs_, set));
        return std::clamp((mixed + apm_.p(mixed, set) + 1) / 2, 1, 4095);
    }

    void update(unsigned bit)
    {
        mixer_.update(inputs_, bit, 2);
        apm_.update(bit);
        Counter::update(order1_[i1_], bit, 4);
        Counter::update(order1_[i1_ + 1], bit, 255);
        Counter::update(order2_[o2_], bit, 30);
        if (expected_bit_ < 2)
        {
            Counter::update(run_counters_[run_slot_], bit, 255);
        }
        partial_ = (partial_ << 1U) | bit;
        if (++bit_ == 8)
        {
            const auto byte = static_cast<uint8_t>(partial_ & 0xFFU);
            run_ = byte == c1_ ? run_ + 1 : 0;
            c2_ = c1_;
            c1_ = byte;
            h2_ = static_cast<std::size_t>(mix64((uint64_t{c2_} << 8U) | c1_) >>
                                           (64U - order2_bits)) &
                  ~std::size_t{0xFF};
            bit_ = 0;
            partial_ = 1;
        }
    }

private:
    static constexpr unsigned order2_bits = 20;
    static constexpr std::size_t bwt_inputs = 5;

    std::vector<uint32_t> order1_; // fast and slow side by side
    std::vector<uint32_t> order2_;
    std::vector<uint32_t> run_counters_;
    Mixer mixer_;
    Apm apm_;
    std::array<int, bwt_inputs> inputs_{};
    uint8_t c1_ = 0;
    uint8_t c2_ = 0;
    std::size_t h2_ = 0;
    std::size_t i1_ = 0;
    std::size_t o2_ = 0;
    std::size_t run_ = 0;
    std::size_t run_slot_ = 0;
    unsigned expected_bit_ = 2;
    unsigned partial_ = 1;
    unsigned bit_ = 0;
};

// A binary arithmetic coder over a model's 12-bit predictions, most significant bit first.
template <class Model>
void encode_bits(Model& model, std::span<const uint8_t> bytes, std::vector<uint8_t>& out)
{
    uint32_t low = 0;
    uint32_t high = 0xFFFFFFFFU;
    for (const uint8_t byte : bytes)
    {
        for (unsigned i = 8; i-- > 0;)
        {
            const unsigned bit = (unsigned{byte} >> i) & 1U;
            const uint32_t mid =
                low + static_cast<uint32_t>(
                          (uint64_t{high - low} * static_cast<uint32_t>(model.p())) >> 12U);
            if (bit != 0)
            {
                high = mid;
            }
            else
            {
                low = mid + 1;
            }
            model.update(bit);
            while (((low ^ high) & 0xFF000000U) == 0)
            {
                out.push_back(static_cast<uint8_t>(high >> 24U));
                low <<= 8U;
                high = (high << 8U) | 0xFFU;
            }
        }
    }
    for (int i = 0; i < 4; ++i)
    {
        out.push_back(static_cast<uint8_t>(low >> 24U));
        low <<= 8U;
    }
}

// Fills `decoded`; false when the stream ends early or leaves bytes over.
template <class Model>
bool decode_bits(Model& model, std::span<const uint8_t> coded, std::span<uint8_t> decoded)
{
    uint32_t low = 0;
    uint32_t high = 0xFFFFFFFFU;
    uint32_t x = 0;
    std::size_t next = 0;
    const auto byte_in = [&] {
        const uint8_t byte = next < coded.size() ? coded[next] : uint8_t{0};
        ++next;
        return byte;
    };
    for (int i = 0; i < 4; ++i)
    {
        x = (x << 8U) | byte_in();
    }
    for (uint8_t& target : decoded)
    {
        unsigned byte = 0;
        for (int i = 0; i < 8; ++i)
        {
            const uint32_t mid =
                low + static_cast<uint32_t>(
                          (uint64_t{high - low} * static_cast<uint32_t>(model.p())) >> 12U);
            const unsigned bit = x <= mid ? 1U : 0U;
            if (bit != 0)
            {
                high = mid;
            }
            else
            {
                low = mid + 1;
            }
            byte = (byte << 1U) | bit;
            if (i == 7)
            {
                target = static_cast<uint8_t>(byte);
            }
            model.update(bit);
            while (((low ^ high) & 0xFF000000U) == 0)
            {
                low <<= 8U;
                high = (high << 8U) | 0xFFU;
                x = (x << 8U) | byte_in();
            }
        }
        if (next > coded.size())
        {
            return false;
        }
    }
    return next == coded.size();
}

} // namespace

std::vector<uint8_t> cm_encode(std::span<const uint8_t> bytes, unsigned table_log2)
{
    table_log2 = std::clamp(table_log2, cm_min_table_log2, cm_max_table_log2);
    std::vector<uint8_t> out{static_cast<uint8_t>(table_log2)};
    Model model(bytes, table_log2);
    encode_bits(model, bytes, out);
    return out;
}

std::expected<void, Error> cm_decode(std::span<const uint8_t> payload, std::vector<uint8_t>& out,
                                     std::size_t size)
{
    if (payload.empty() || payload[0] < cm_min_table_log2 || payload[0] > cm_max_table_log2)
    {
        return std::unexpected(Error::corrupt_payload);
    }
    const std::size_t base = out.size();
    out.resize(base + size);
    const std::span<uint8_t> decoded(out.data() + base, size);
    Model model(decoded, payload[0]);
    if (!decode_bits(model, payload.subspan(1), decoded))
    {
        out.resize(base);
        return std::unexpected(Error::corrupt_payload);
    }
    return {};
}

std::vector<uint8_t> bwt_cm_encode(std::span<const uint8_t> bytes)
{
    std::vector<uint8_t> transformed;
    const uint32_t primary = forward_bwt(bytes, transformed);
    std::vector<uint8_t> out;
    for (unsigned shift = 0; shift < 32; shift += 8)
    {
        out.push_back(static_cast<uint8_t>(primary >> shift));
    }
    BwtModel model;
    encode_bits(model, transformed, out);
    return out;
}

std::expected<void, Error> bwt_cm_decode(std::span<const uint8_t> payload,
                                         std::vector<uint8_t>& out, std::size_t size)
{
    if (payload.size() < 4)
    {
        return std::unexpected(Error::corrupt_payload);
    }
    uint32_t primary = 0;
    for (unsigned i = 0; i < 4; ++i)
    {
        primary |= uint32_t{payload[i]} << (8 * i);
    }
    std::vector<uint8_t> transformed(size);
    BwtModel model;
    if (!decode_bits(model, payload.subspan(4), transformed))
    {
        return std::unexpected(Error::corrupt_payload);
    }
    return inverse_bwt(transformed, primary, out);
}

} // namespace compressor
