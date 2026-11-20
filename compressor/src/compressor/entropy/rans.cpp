#include <compressor/entropy/rans.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <numeric>
#include <span>
#include <vector>

#include <compressor/error.h>

namespace compressor
{

namespace
{

constexpr uint32_t total = uint32_t{1} << rans_scale_bits;
constexpr uint32_t lower_bound = uint32_t{1} << 23U; // the state stays in [2^23, 2^31)

std::unexpected<Error> corrupt()
{
    return std::unexpected(Error::corrupt_payload);
}

// Frequencies summing to `total`, every used symbol at least 1.
std::vector<uint32_t> normalise(std::span<const uint64_t> counts)
{
    const uint64_t sum = std::accumulate(counts.begin(), counts.end(), uint64_t{0});
    std::vector<uint32_t> scaled(counts.size());
    if (sum == 0)
    {
        return scaled;
    }
    uint32_t assigned = 0;
    std::size_t largest = 0;
    for (std::size_t s = 0; s < counts.size(); ++s)
    {
        if (counts[s] == 0)
        {
            continue;
        }
        scaled[s] = std::max<uint32_t>(1, static_cast<uint32_t>((counts[s] * total) / sum));
        assigned += scaled[s];
        if (counts[s] > counts[largest])
        {
            largest = s;
        }
    }
    // Rounding leaves the sum off by a little: take it from (or give it to) the largest
    // symbols, never below 1.
    while (assigned != total)
    {
        if (assigned < total)
        {
            scaled[largest] += total - assigned;
            break;
        }
        const uint32_t excess = assigned - total;
        std::size_t biggest = largest;
        for (std::size_t s = 0; s < scaled.size(); ++s)
        {
            if (scaled[s] > scaled[biggest])
            {
                biggest = s;
            }
        }
        const uint32_t take = std::min(excess, scaled[biggest] - 1);
        scaled[biggest] -= take;
        assigned -= take;
    }
    return scaled;
}

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
    for (unsigned shift = 0; shift < 21; shift += 7)
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

} // namespace

void rans_encode(std::span<const uint16_t> symbols, std::size_t alphabet, std::vector<uint8_t>& out)
{
    std::vector<uint64_t> counts(alphabet);
    for (const uint16_t symbol : symbols)
    {
        ++counts[symbol];
    }
    const std::vector<uint32_t> frequency = normalise(counts);
    std::vector<uint32_t> start(alphabet);
    std::exclusive_scan(frequency.begin(), frequency.end(), start.begin(), uint32_t{0});
    for (const uint32_t value : frequency)
    {
        put_varint(out, value);
    }
    if (symbols.empty())
    {
        return; // the table alone, all zeros
    }

    // rANS encodes backwards and its bytes come out reversed.
    std::vector<uint8_t> reversed;
    reversed.reserve(symbols.size() / 2);
    uint32_t state = lower_bound;
    for (std::size_t i = symbols.size(); i-- > 0;)
    {
        const uint32_t f = frequency[symbols[i]];
        const uint32_t limit = ((lower_bound >> rans_scale_bits) << 8U) * f;
        while (state >= limit)
        {
            reversed.push_back(static_cast<uint8_t>(state));
            state >>= 8U;
        }
        state = ((state / f) << rans_scale_bits) + (state % f) + start[symbols[i]];
    }
    for (unsigned i = 0; i < 4; ++i)
    {
        reversed.push_back(static_cast<uint8_t>(state >> (8 * i)));
    }
    out.insert(out.end(), reversed.rbegin(), reversed.rend());
}

std::expected<std::vector<uint16_t>, Error> rans_decode(std::span<const uint8_t> in,
                                                        std::size_t alphabet, std::size_t count)
{
    std::size_t at = 0;
    std::vector<uint32_t> frequency(alphabet);
    uint64_t sum = 0;
    for (uint32_t& value : frequency)
    {
        if (!get_varint(in, at, value) || value > total)
        {
            return corrupt();
        }
        sum += value;
    }
    if (count == 0)
    {
        return at == in.size() ? std::vector<uint16_t>{}
                               : std::expected<std::vector<uint16_t>, Error>(corrupt());
    }
    if (sum != total)
    {
        return corrupt();
    }
    std::vector<uint16_t> slot_symbol(total);
    std::vector<uint32_t> start(alphabet);
    uint32_t next = 0;
    for (std::size_t s = 0; s < alphabet; ++s)
    {
        start[s] = next;
        std::fill_n(slot_symbol.begin() + next, frequency[s], static_cast<uint16_t>(s));
        next += frequency[s];
    }

    if (in.size() - at < 4)
    {
        return corrupt();
    }
    // The encoder wrote its final state first, most significant byte first.
    uint32_t state = 0;
    for (unsigned i = 0; i < 4; ++i)
    {
        state = (state << 8U) | in[at++];
    }
    std::vector<uint16_t> symbols(count);
    for (std::size_t i = 0; i < count; ++i)
    {
        if (state < lower_bound)
        {
            return corrupt();
        }
        const uint32_t slot = state & (total - 1);
        const uint16_t symbol = slot_symbol[slot];
        symbols[i] = symbol;
        state = (frequency[symbol] * (state >> rans_scale_bits)) + slot - start[symbol];
        while (state < lower_bound)
        {
            if (at >= in.size())
            {
                return corrupt();
            }
            state = (state << 8U) | in[at++];
        }
    }
    if (state != lower_bound || at != in.size())
    {
        return corrupt();
    }
    return symbols;
}

} // namespace compressor
