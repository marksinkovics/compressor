#include <compressor/ppm/ppm.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <utility>
#include <vector>

#include <compressor/error.h>

namespace compressor
{

namespace
{

constexpr unsigned max_order = 5;
constexpr unsigned table_bits = 18;
constexpr uint32_t max_total = 8000; // counts are halved above this
constexpr uint32_t top = uint32_t{1} << 24U;
constexpr uint32_t bottom = uint32_t{1} << 16U;

struct Symbol
{
    uint8_t byte = 0;
    uint16_t count = 0;
};

struct Context
{
    uint64_t check = 0;
    uint32_t total = 0;
    std::vector<Symbol> symbols;
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

// Subbotin's carry-less range coder; totals stay below 2^16.
class Encoder
{
public:
    explicit Encoder(std::vector<uint8_t>& out)
        : out_(out)
    {
    }

    void code(uint32_t cumulative, uint32_t count, uint32_t total)
    {
        range_ /= total;
        low_ += cumulative * range_;
        range_ *= count;
        normalise();
    }

    void finish()
    {
        for (int i = 0; i < 4; ++i)
        {
            out_.push_back(static_cast<uint8_t>(low_ >> 24U));
            low_ <<= 8U;
        }
    }

private:
    void normalise()
    {
        while (true)
        {
            if ((low_ ^ (low_ + range_)) >= top)
            {
                if (range_ >= bottom)
                {
                    return;
                }
                range_ = (0U - low_) & (bottom - 1);
            }
            out_.push_back(static_cast<uint8_t>(low_ >> 24U));
            low_ <<= 8U;
            range_ <<= 8U;
        }
    }

    std::vector<uint8_t>& out_;
    uint32_t low_ = 0;
    uint32_t range_ = 0xFFFFFFFFU;
};

class Decoder
{
public:
    explicit Decoder(std::span<const uint8_t> in)
        : in_(in)
    {
        for (int i = 0; i < 4; ++i)
        {
            code_ = (code_ << 8U) | next();
        }
    }

    // The count position the next symbol covers; false for a damaged stream.
    bool target(uint32_t total, uint32_t& value)
    {
        range_ /= total;
        value = (code_ - low_) / range_;
        return value < total;
    }

    void code(uint32_t cumulative, uint32_t count)
    {
        low_ += cumulative * range_;
        range_ *= count;
        while (true)
        {
            if ((low_ ^ (low_ + range_)) >= top)
            {
                if (range_ >= bottom)
                {
                    return;
                }
                range_ = (0U - low_) & (bottom - 1);
            }
            code_ = (code_ << 8U) | next();
            low_ <<= 8U;
            range_ <<= 8U;
        }
    }

    // Every byte read, the 4 flush bytes included, and no more.
    bool clean() const
    {
        return next_ == in_.size();
    }

    bool overrun() const
    {
        return next_ > in_.size();
    }

private:
    uint8_t next()
    {
        const uint8_t byte = next_ < in_.size() ? in_[next_] : uint8_t{0};
        ++next_;
        return byte;
    }

    std::span<const uint8_t> in_;
    std::size_t next_ = 0;
    uint32_t low_ = 0;
    uint32_t range_ = 0xFFFFFFFFU;
    uint32_t code_ = 0;
};

class Model
{
public:
    Model()
    {
        for (auto& table : tables_)
        {
            table.resize(std::size_t{1} << table_bits);
        }
    }

    // The contexts for the next byte, from order max_order (or as many bytes as there are) down.
    void find_contexts(std::span<const uint8_t> history, std::size_t position)
    {
        contexts_[0] = &order0_;
        uint64_t key = 0;
        for (unsigned order = 1; order <= max_order; ++order)
        {
            contexts_[order] = nullptr;
            if (order > position)
            {
                continue;
            }
            key = (key << 8U) | history[position - order];
            const uint64_t h = mix64(key + (uint64_t{order} << 56U));
            Context& slot = tables_[order - 1][h >> (64U - table_bits)];
            if (slot.check != h)
            {
                slot = Context{.check = h, .total = 0, .symbols = {}};
            }
            contexts_[order] = &slot;
        }
        ++stamp_;
        excluded_count_ = 0;
    }

    template <class Code> bool encode(uint8_t byte, Code&& code)
    {
        int found = -1;
        for (int order = max_order; order >= 0; --order)
        {
            Context* context = contexts_[order];
            if (context == nullptr || context->symbols.empty())
            {
                continue;
            }
            uint32_t total = 0;
            uint32_t distinct = 0;
            uint32_t cumulative = 0;
            uint32_t count = 0;
            for (const Symbol& symbol : context->symbols)
            {
                if (excluded_[symbol.byte] == stamp_)
                {
                    continue;
                }
                if (symbol.byte == byte)
                {
                    cumulative = total;
                    count = symbol.count;
                }
                total += symbol.count;
                ++distinct;
            }
            if (distinct == 0)
            {
                continue;
            }
            if (count > 0)
            {
                code(cumulative, count, total + distinct);
                found = order;
                break;
            }
            code(total, distinct, total + distinct);
            exclude(*context);
        }
        if (found < 0)
        {
            uint32_t rank = 0;
            for (unsigned b = 0; b < byte; ++b)
            {
                rank += excluded_[b] == stamp_ ? 0 : 1;
            }
            code(rank, 1, 256 - excluded_count_);
        }
        update(byte, found);
        return true;
    }

    // Returns false for a damaged stream.
    bool decode(Decoder& decoder, uint8_t& byte)
    {
        int found = -1;
        for (int order = max_order; order >= 0 && found < 0; --order)
        {
            Context* context = contexts_[order];
            if (context == nullptr || context->symbols.empty())
            {
                continue;
            }
            uint32_t total = 0;
            uint32_t distinct = 0;
            for (const Symbol& symbol : context->symbols)
            {
                if (excluded_[symbol.byte] != stamp_)
                {
                    total += symbol.count;
                    ++distinct;
                }
            }
            if (distinct == 0)
            {
                continue;
            }
            uint32_t target = 0;
            if (!decoder.target(total + distinct, target))
            {
                return false;
            }
            if (target >= total)
            {
                decoder.code(total, distinct);
                exclude(*context);
                continue;
            }
            uint32_t cumulative = 0;
            for (const Symbol& symbol : context->symbols)
            {
                if (excluded_[symbol.byte] == stamp_)
                {
                    continue;
                }
                if (target < cumulative + symbol.count)
                {
                    decoder.code(cumulative, symbol.count);
                    byte = symbol.byte;
                    found = order;
                    break;
                }
                cumulative += symbol.count;
            }
        }
        if (found < 0)
        {
            uint32_t rank = 0;
            if (!decoder.target(256 - excluded_count_, rank))
            {
                return false;
            }
            decoder.code(rank, 1);
            for (unsigned b = 0; b < 256; ++b)
            {
                if (excluded_[b] == stamp_)
                {
                    continue;
                }
                if (rank-- == 0)
                {
                    byte = static_cast<uint8_t>(b);
                    break;
                }
            }
        }
        update(byte, found);
        return true;
    }

private:
    void exclude(const Context& context)
    {
        for (const Symbol& symbol : context.symbols)
        {
            if (excluded_[symbol.byte] != stamp_)
            {
                excluded_[symbol.byte] = stamp_;
                ++excluded_count_;
            }
        }
    }

    // Update exclusion: only the orders from the one that coded the byte up.
    void update(uint8_t byte, int found)
    {
        for (int order = std::max(found, 0); std::cmp_less_equal(order, max_order); ++order)
        {
            Context* context = contexts_[order];
            if (context == nullptr)
            {
                continue;
            }
            auto symbol = std::ranges::find(context->symbols, byte, &Symbol::byte);
            if (symbol == context->symbols.end())
            {
                context->symbols.push_back({.byte = byte, .count = 1});
                ++context->total;
            }
            else
            {
                symbol->count = static_cast<uint16_t>(symbol->count + 1);
                ++context->total;
            }
            if (context->total > max_total)
            {
                context->total = 0;
                for (Symbol& s : context->symbols)
                {
                    s.count = static_cast<uint16_t>((s.count + 1) / 2);
                    context->total += s.count;
                }
            }
        }
    }

    std::array<std::vector<Context>, max_order> tables_;
    Context order0_;
    std::array<Context*, max_order + 1> contexts_{};
    std::array<uint32_t, 256> excluded_{};
    uint32_t stamp_ = 0;
    uint32_t excluded_count_ = 0;
};

} // namespace

std::vector<uint8_t> ppm_encode(std::span<const uint8_t> bytes)
{
    std::vector<uint8_t> out;
    Encoder encoder(out);
    Model model;
    for (std::size_t i = 0; i < bytes.size(); ++i)
    {
        model.find_contexts(bytes, i);
        model.encode(bytes[i], [&](uint32_t cumulative, uint32_t count, uint32_t total) {
            encoder.code(cumulative, count, total);
        });
    }
    encoder.finish();
    return out;
}

std::expected<void, Error> ppm_decode(std::span<const uint8_t> payload, std::vector<uint8_t>& out,
                                      std::size_t size)
{
    const std::size_t start = out.size();
    out.resize(start + size);
    const std::span<uint8_t> decoded(out.data() + start, size);
    Decoder decoder(payload);
    Model model;
    for (std::size_t i = 0; i < size; ++i)
    {
        model.find_contexts(decoded, i);
        if (!model.decode(decoder, decoded[i]) || decoder.overrun())
        {
            out.resize(start);
            return std::unexpected(Error::corrupt_payload);
        }
    }
    if (!decoder.clean())
    {
        out.resize(start);
        return std::unexpected(Error::corrupt_payload);
    }
    return {};
}

} // namespace compressor
