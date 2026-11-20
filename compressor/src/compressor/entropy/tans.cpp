#include <compressor/entropy/tans.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <span>
#include <vector>

#include <compressor/error.h>

namespace compressor
{

namespace
{

constexpr uint32_t table_size = uint32_t{1} << tans_table_log;
constexpr std::size_t alphabet = 256;
constexpr std::size_t states = 2;

using Frequencies = std::array<uint32_t, alphabet>;

std::unexpected<Error> corrupt()
{
    return std::unexpected(Error::corrupt_payload);
}

unsigned highbit(uint32_t value)
{
    return static_cast<unsigned>(std::bit_width(value)) - 1;
}

// Frequencies summing to table_size, every used byte at least 1; the rounding error goes to (or
// comes from) the largest.
Frequencies normalise(std::span<const uint8_t> bytes)
{
    std::array<uint64_t, alphabet> counts{};
    for (const uint8_t byte : bytes)
    {
        ++counts[byte];
    }
    Frequencies scaled{};
    uint32_t assigned = 0;
    for (std::size_t s = 0; s < alphabet; ++s)
    {
        if (counts[s] != 0)
        {
            const uint64_t rounded = ((counts[s] * table_size) + (bytes.size() / 2)) / bytes.size();
            scaled[s] = std::max<uint32_t>(1, static_cast<uint32_t>(rounded));
            assigned += scaled[s];
        }
    }
    while (assigned != table_size)
    {
        const auto largest = static_cast<std::size_t>(
            std::distance(scaled.begin(), std::ranges::max_element(scaled)));
        if (assigned < table_size)
        {
            scaled[largest] += table_size - assigned;
            break;
        }
        const uint32_t take = std::min(assigned - table_size, scaled[largest] - 1);
        scaled[largest] -= take;
        assigned -= take;
    }
    return scaled;
}

// FSE's spread: each byte's states scattered over the table by a step coprime to its size.
std::vector<uint8_t> spread(const Frequencies& frequency)
{
    constexpr uint32_t step = (table_size >> 1U) + (table_size >> 3U) + 3;
    std::vector<uint8_t> symbol(table_size);
    uint32_t position = 0;
    for (std::size_t s = 0; s < alphabet; ++s)
    {
        for (uint32_t i = 0; i < frequency[s]; ++i)
        {
            symbol[position] = static_cast<uint8_t>(s);
            position = (position + step) & (table_size - 1);
        }
    }
    return symbol;
}

class BitWriter
{
public:
    explicit BitWriter(std::vector<uint8_t>& out)
        : out_(out)
    {
    }

    void put(uint32_t value, unsigned bits)
    {
        container_ |= uint64_t{value & ((uint32_t{1} << bits) - 1)} << count_;
        count_ += bits;
        while (count_ >= 8)
        {
            out_.push_back(static_cast<uint8_t>(container_));
            container_ >>= 8U;
            count_ -= 8;
        }
    }

    // A 1 after the last bit, so the reader finds the end.
    void finish()
    {
        put(1, 1);
        if (count_ > 0)
        {
            out_.push_back(static_cast<uint8_t>(container_));
        }
    }

private:
    std::vector<uint8_t>& out_;
    uint64_t container_ = 0;
    unsigned count_ = 0;
};

// Reads the bits back from the end, the last written first.
class BitReader
{
public:
    explicit BitReader(std::span<const uint8_t> in)
        : in_(in)
    {
        if (!in.empty() && in.back() != 0)
        {
            position_ = ((in.size() - 1) * 8) + highbit(in.back());
            valid_ = true;
        }
    }

    bool valid() const
    {
        return valid_;
    }

    bool get(unsigned bits, uint32_t& value)
    {
        if (bits > position_)
        {
            return false;
        }
        position_ -= bits;
        const std::size_t byte = position_ >> 3U;
        uint64_t word = 0;
        if (byte + 8 <= in_.size())
        {
            std::memcpy(&word, in_.data() + byte, 8);
            if constexpr (std::endian::native == std::endian::big)
            {
                word = std::byteswap(word);
            }
        }
        else
        {
            for (std::size_t i = byte; i < in_.size(); ++i)
            {
                word |= uint64_t{in_[i]} << (8 * (i - byte));
            }
        }
        value = static_cast<uint32_t>(word >> (position_ & 7U)) & ((uint32_t{1} << bits) - 1);
        return true;
    }

    bool empty() const
    {
        return position_ == 0;
    }

private:
    std::span<const uint8_t> in_;
    std::size_t position_ = 0; // bits left to read
    bool valid_ = false;
};

void put_varint(std::vector<uint8_t>& out, uint32_t value)
{
    while (value >= 0x80)
    {
        out.push_back(static_cast<uint8_t>(value | 0x80U));
        value >>= 7U;
    }
    out.push_back(static_cast<uint8_t>(value));
}

bool get_varint(std::span<const uint8_t> in, std::size_t& at, uint32_t& value)
{
    value = 0;
    for (unsigned shift = 0; shift < 14; shift += 7)
    {
        if (at >= in.size())
        {
            return false;
        }
        const uint8_t byte = in[at++];
        value |= uint32_t{byte & 0x7FU} << shift;
        if ((byte & 0x80U) == 0)
        {
            return true;
        }
    }
    return false;
}

struct DecodeEntry
{
    uint16_t next = 0; // the next state before its bits are added
    uint8_t symbol = 0;
    uint8_t bits = 0;
};

} // namespace

void tans_encode(std::span<const uint8_t> bytes, std::vector<uint8_t>& out)
{
    if (bytes.empty())
    {
        out.insert(out.end(), alphabet, 0);
        return;
    }
    const Frequencies frequency = normalise(bytes);
    for (const uint32_t value : frequency)
    {
        put_varint(out, value);
    }

    // zstd's FSE_buildCTable: the states of each byte in table order, and per byte the numbers
    // that turn a state into its bit count and next state without a division.
    const std::vector<uint8_t> symbol = spread(frequency);
    std::array<uint32_t, alphabet + 1> cumulative{};
    for (std::size_t s = 0; s < alphabet; ++s)
    {
        cumulative[s + 1] = cumulative[s] + frequency[s];
    }
    std::vector<uint16_t> next_state(table_size);
    {
        std::array<uint32_t, alphabet + 1> fill = cumulative;
        for (uint32_t u = 0; u < table_size; ++u)
        {
            next_state[fill[symbol[u]]++] = static_cast<uint16_t>(table_size + u);
        }
    }
    std::array<uint32_t, alphabet> delta_bits{};
    std::array<int32_t, alphabet> delta_state{};
    for (std::size_t s = 0; s < alphabet; ++s)
    {
        const uint32_t f = frequency[s];
        if (f == 0)
        {
            continue;
        }
        const unsigned max_bits = tans_table_log - (f == 1 ? 0 : highbit(f - 1));
        delta_bits[s] = (max_bits << 16U) - (f << max_bits);
        delta_state[s] = static_cast<int32_t>(cumulative[s]) - static_cast<int32_t>(f);
    }

    BitWriter writer(out);
    std::array<uint32_t, states> state{table_size, table_size};
    for (std::size_t i = bytes.size(); i-- > 0;)
    {
        uint32_t& current = state[i % states];
        const uint8_t s = bytes[i];
        const uint32_t bits = (current + delta_bits[s]) >> 16U;
        writer.put(current, bits);
        const int32_t slot = static_cast<int32_t>(current >> bits) + delta_state[s];
        current = next_state[static_cast<std::size_t>(slot)];
    }
    for (std::size_t k = states; k-- > 0;)
    {
        writer.put(state[k], tans_table_log);
    }
    writer.finish();
}

std::expected<void, Error> tans_decode(std::span<const uint8_t> in, std::size_t count,
                                       std::vector<uint8_t>& out)
{
    std::size_t at = 0;
    Frequencies frequency{};
    uint32_t sum = 0;
    for (uint32_t& value : frequency)
    {
        if (!get_varint(in, at, value) || value > table_size)
        {
            return corrupt();
        }
        sum += value;
    }
    if (count == 0)
    {
        return sum == 0 && at == in.size() ? std::expected<void, Error>{} : corrupt();
    }
    if (sum != table_size)
    {
        return corrupt();
    }

    const std::vector<uint8_t> symbol = spread(frequency);
    std::vector<DecodeEntry> table(table_size);
    {
        Frequencies next = frequency;
        for (uint32_t u = 0; u < table_size; ++u)
        {
            const uint8_t s = symbol[u];
            const uint32_t x = next[s]++;
            const unsigned bits = tans_table_log - highbit(x);
            table[u] = {.next = static_cast<uint16_t>((x << bits) - table_size),
                        .symbol = s,
                        .bits = static_cast<uint8_t>(bits)};
        }
    }

    BitReader reader(in.subspan(at));
    if (!reader.valid())
    {
        return corrupt();
    }
    std::array<uint32_t, states> state{};
    for (uint32_t& s : state)
    {
        if (!reader.get(tans_table_log, s))
        {
            return corrupt();
        }
    }
    const std::size_t base = out.size();
    out.resize(base + count);
    uint8_t* target = out.data() + base;
    for (std::size_t i = 0; i < count; ++i)
    {
        uint32_t& current = state[i % states];
        const DecodeEntry entry = table[current];
        target[i] = entry.symbol;
        uint32_t bits = 0;
        if (!reader.get(entry.bits, bits))
        {
            out.resize(base);
            return corrupt();
        }
        current = entry.next + bits;
    }
    // Both states are back where the encoder started, and every bit is used.
    if (state[0] != 0 || state[1] != 0 || !reader.empty())
    {
        out.resize(base);
        return corrupt();
    }
    return {};
}

} // namespace compressor
