#include <compressor/bwt/bwt.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <numeric>
#include <span>
#include <vector>

#include <compressor/error.h>
#include <compressor/huffman/huffman.h>

namespace compressor
{

namespace
{

// Symbols after move-to-front: RUNA and RUNB write a run of zeros in bijective base 2; MTF index
// i (1-255) becomes i + 1.
constexpr uint16_t run_a = 0;
constexpr uint16_t run_b = 1;
constexpr std::size_t alphabet = 257;
constexpr unsigned table_bits = max_code_length;

std::unexpected<Error> corrupt()
{
    return std::unexpected(Error::corrupt_payload);
}

void put_le32(std::vector<uint8_t>& out, uint32_t value)
{
    for (unsigned i = 0; i < 4; ++i)
    {
        out.push_back(static_cast<uint8_t>(value >> (8 * i)));
    }
}

uint32_t get_le32(std::span<const uint8_t> bytes)
{
    return static_cast<uint32_t>(bytes[0]) | (static_cast<uint32_t>(bytes[1]) << 8U) |
           (static_cast<uint32_t>(bytes[2]) << 16U) | (static_cast<uint32_t>(bytes[3]) << 24U);
}

void write_run(std::vector<uint16_t>& symbols, std::size_t run)
{
    // Bijective base 2: digits 1 (RUNA) and 2 (RUNB), least significant first.
    while (run > 0)
    {
        if ((run & 1U) != 0)
        {
            symbols.push_back(run_a);
            run = (run - 1) / 2;
        }
        else
        {
            symbols.push_back(run_b);
            run = (run - 2) / 2;
        }
    }
}

std::vector<uint16_t> move_to_front(std::span<const uint8_t> bytes)
{
    std::array<uint8_t, 256> order{};
    std::ranges::iota(order, uint8_t{0});
    std::vector<uint16_t> symbols;
    symbols.reserve(bytes.size());
    std::size_t zeros = 0;
    for (const uint8_t byte : bytes)
    {
        const auto at = static_cast<std::size_t>(std::ranges::find(order, byte) - order.begin());
        if (at == 0)
        {
            ++zeros;
            continue;
        }
        write_run(symbols, zeros);
        zeros = 0;
        std::copy_backward(order.begin(), order.begin() + static_cast<std::ptrdiff_t>(at),
                           order.begin() + static_cast<std::ptrdiff_t>(at) + 1);
        order[0] = byte;
        symbols.push_back(static_cast<uint16_t>(at + 1));
    }
    write_run(symbols, zeros);
    return symbols;
}

class BitWriter
{
public:
    explicit BitWriter(std::vector<uint8_t>& out)
        : out_(out)
    {
    }

    void put(uint32_t bits, unsigned count)
    {
        buffer_ = (buffer_ << count) | bits;
        filled_ += count;
        while (filled_ >= 8)
        {
            filled_ -= 8;
            out_.push_back(static_cast<uint8_t>(buffer_ >> filled_));
        }
    }

    void flush()
    {
        if (filled_ > 0)
        {
            put(0, 8 - filled_);
        }
    }

private:
    std::vector<uint8_t>& out_;
    uint64_t buffer_ = 0;
    unsigned filled_ = 0;
};

// MSB-first codes, decoded through a table indexed by the next table_bits bits.
class Decoder
{
public:
    bool build(std::span<const uint8_t> lengths)
    {
        std::vector<uint8_t> copy(lengths.begin(), lengths.end());
        uint64_t kraft = 0;
        std::size_t used = 0;
        for (const uint8_t length : copy)
        {
            if (length > table_bits)
            {
                return false;
            }
            if (length > 0)
            {
                kraft += uint64_t{1} << (table_bits - length);
                ++used;
            }
        }
        const bool single = used == 1 && kraft == uint64_t{1} << (table_bits - 1);
        if (used == 0 || (!single && kraft != uint64_t{1} << table_bits))
        {
            return false;
        }
        entries_.assign(std::size_t{1} << table_bits, {});
        const std::vector<Code> codes = canonical_codes(copy);
        for (std::size_t symbol = 0; symbol < codes.size(); ++symbol)
        {
            const Code& code = codes[symbol];
            if (code.length == 0)
            {
                continue;
            }
            const std::size_t first = std::size_t{code.bits} << (table_bits - code.length);
            std::fill_n(entries_.begin() + static_cast<std::ptrdiff_t>(first),
                        std::size_t{1} << (table_bits - code.length),
                        Entry{.symbol = static_cast<uint16_t>(symbol), .length = code.length});
        }
        return true;
    }

    // False at the end of the payload or on bits that start no code.
    bool next(std::span<const uint8_t> payload, uint64_t& bit, uint16_t& symbol) const
    {
        uint32_t window = 0;
        for (unsigned i = 0; i < table_bits; ++i)
        {
            const uint64_t at = bit + i;
            const unsigned value =
                at / 8 < payload.size() ? (unsigned{payload[at / 8]} >> (7U - (at % 8))) & 1U : 0;
            window = (window << 1U) | value;
        }
        const Entry entry = entries_[window];
        if (entry.length == 0 || bit + entry.length > payload.size() * 8)
        {
            return false;
        }
        bit += entry.length;
        symbol = entry.symbol;
        return true;
    }

private:
    struct Entry
    {
        uint16_t symbol = 0;
        uint8_t length = 0;
    };
    std::vector<Entry> entries_;
};

} // namespace

std::vector<uint32_t> suffix_array(std::span<const uint8_t> bytes)
{
    const std::size_t n = bytes.size();
    std::vector<uint32_t> sa(n);
    if (n == 0)
    {
        return sa;
    }
    // rank 0 is "past the end", so a suffix sorts before every longer one it prefixes.
    std::vector<uint32_t> rank(n);
    std::vector<uint32_t> next_rank(n);
    std::vector<uint32_t> buffer(n);
    for (std::size_t i = 0; i < n; ++i)
    {
        rank[i] = uint32_t{bytes[i]} + 1;
    }
    std::size_t ranks = 257;
    std::vector<uint32_t> count;
    for (std::size_t k = 1;; k *= 2)
    {
        const auto second = [&](std::size_t i) {
            return i + k < n ? rank[i + k] : 0U;
        };
        // Radix sort by (rank[i], rank[i + k]): second key, then a stable pass on the first.
        count.assign(ranks + 1, 0);
        for (std::size_t i = 0; i < n; ++i)
        {
            ++count[second(i)];
        }
        std::partial_sum(count.begin(), count.end(), count.begin());
        for (std::size_t i = n; i-- > 0;)
        {
            buffer[--count[second(i)]] = static_cast<uint32_t>(i);
        }
        count.assign(ranks + 1, 0);
        for (std::size_t i = 0; i < n; ++i)
        {
            ++count[rank[i]];
        }
        std::partial_sum(count.begin(), count.end(), count.begin());
        for (std::size_t j = n; j-- > 0;)
        {
            const uint32_t i = buffer[j];
            sa[--count[rank[i]]] = i;
        }
        next_rank[sa[0]] = 1;
        for (std::size_t j = 1; j < n; ++j)
        {
            const uint32_t a = sa[j - 1];
            const uint32_t b = sa[j];
            const bool same = rank[a] == rank[b] && second(a) == second(b);
            next_rank[b] = next_rank[a] + (same ? 0 : 1);
        }
        rank.swap(next_rank);
        ranks = rank[sa[n - 1]];
        if (ranks == n)
        {
            return sa;
        }
    }
}

uint32_t forward_bwt(std::span<const uint8_t> bytes, std::vector<uint8_t>& out)
{
    const std::vector<uint32_t> sa = suffix_array(bytes);
    out.clear();
    out.reserve(bytes.size());
    // With the end marker as a virtual first row, the suffix starting at 0 is preceded by it.
    uint32_t primary = 0;
    out.push_back(bytes.empty() ? 0 : bytes.back()); // the row of the end marker itself
    for (std::size_t row = 0; row < sa.size(); ++row)
    {
        if (sa[row] == 0)
        {
            primary = static_cast<uint32_t>(row + 1);
            continue;
        }
        out.push_back(bytes[sa[row] - 1]);
    }
    if (bytes.empty())
    {
        out.clear();
    }
    return primary;
}

std::expected<void, Error> inverse_bwt(std::span<const uint8_t> transformed, uint32_t primary,
                                       std::vector<uint8_t>& out)
{
    const std::size_t n = transformed.size();
    if (n == 0)
    {
        return primary == 0 ? std::expected<void, Error>{} : corrupt();
    }
    if (primary == 0 || primary > n)
    {
        return corrupt();
    }
    // Rows 0..n: row 0 is the end marker's, and `primary` holds the marker in the last column.
    // transformed[r] is the last column of row r for r < primary, of row r + 1 after it.
    std::array<uint32_t, 256> count{};
    for (const uint8_t byte : transformed)
    {
        ++count[byte];
    }
    std::array<uint32_t, 256> first{};
    uint32_t total = 1; // the end marker sorts first
    for (std::size_t c = 0; c < 256; ++c)
    {
        first[c] = total;
        total += count[c];
    }
    // lf[row] for every row with a real byte in the last column.
    std::vector<uint32_t> lf(n + 1);
    std::array<uint32_t, 256> seen{};
    for (std::size_t row = 0; row <= n; ++row)
    {
        if (row == primary)
        {
            continue;
        }
        const uint8_t byte = transformed[row < primary ? row : row - 1];
        lf[row] = first[byte] + seen[byte]++;
    }
    const std::size_t start = out.size();
    out.resize(start + n);
    // Walking LF from row 0 (whose last column is the input's last byte) yields the input
    // backwards.
    std::size_t row = 0;
    for (std::size_t i = n; i-- > 0;)
    {
        if (row == primary)
        {
            return corrupt();
        }
        out[start + i] = transformed[row < primary ? row : row - 1];
        row = lf[row];
    }
    if (row != primary)
    {
        out.resize(start);
        return corrupt();
    }
    return {};
}

std::vector<uint8_t> bwt_encode(std::span<const uint8_t> bytes)
{
    std::vector<uint8_t> transformed;
    const uint32_t primary = forward_bwt(bytes, transformed);
    const std::vector<uint16_t> symbols = move_to_front(transformed);

    std::vector<uint64_t> frequencies(alphabet);
    for (const uint16_t symbol : symbols)
    {
        ++frequencies[symbol];
    }
    const std::vector<uint8_t> lengths = code_lengths(frequencies, max_code_length);
    const std::vector<Code> codes = canonical_codes(lengths);

    std::vector<uint8_t> out;
    put_le32(out, primary);
    put_le32(out, static_cast<uint32_t>(symbols.size()));
    for (std::size_t symbol = 0; symbol < alphabet; symbol += 2)
    {
        const uint8_t high = symbol + 1 < alphabet ? lengths[symbol + 1] : 0;
        out.push_back(static_cast<uint8_t>(unsigned{lengths[symbol]} | (unsigned{high} << 4U)));
    }
    BitWriter bits(out);
    for (const uint16_t symbol : symbols)
    {
        bits.put(codes[symbol].bits, codes[symbol].length);
    }
    bits.flush();
    return out;
}

std::expected<void, Error> bwt_decode(std::span<const uint8_t> payload, std::vector<uint8_t>& out,
                                      std::size_t size)
{
    constexpr std::size_t length_bytes = (alphabet + 1) / 2;
    if (payload.size() < 8 + length_bytes)
    {
        return corrupt();
    }
    const uint32_t primary = get_le32(payload);
    const uint32_t symbol_count = get_le32(payload.subspan(4));
    if (symbol_count > size)
    {
        return corrupt();
    }
    std::vector<uint8_t> lengths(alphabet);
    for (std::size_t i = 0; i < length_bytes; ++i)
    {
        lengths[2 * i] = payload[8 + i] & 0x0FU;
        if ((2 * i) + 1 < alphabet)
        {
            lengths[(2 * i) + 1] = payload[8 + i] >> 4U;
        }
    }
    const std::span<const uint8_t> bits_bytes = payload.subspan(8 + length_bytes);

    std::vector<uint8_t> transformed;
    transformed.reserve(size);
    if (symbol_count > 0)
    {
        Decoder decoder;
        if (!decoder.build(lengths))
        {
            return corrupt();
        }
        std::array<uint8_t, 256> order{};
        std::ranges::iota(order, uint8_t{0});
        uint64_t bit = 0;
        std::size_t run = 0;
        std::size_t weight = 1;
        const auto flush_run = [&]() {
            if (run > size - transformed.size())
            {
                return false;
            }
            transformed.insert(transformed.end(), run, order[0]);
            run = 0;
            weight = 1;
            return true;
        };
        for (uint32_t i = 0; i < symbol_count; ++i)
        {
            uint16_t symbol = 0;
            if (!decoder.next(bits_bytes, bit, symbol))
            {
                return corrupt();
            }
            if (symbol == run_a || symbol == run_b)
            {
                run += weight * (symbol == run_a ? 1 : 2);
                weight *= 2;
                if (run > size)
                {
                    return corrupt();
                }
                continue;
            }
            if (!flush_run() || transformed.size() >= size)
            {
                return corrupt();
            }
            const std::size_t at = symbol - 1U;
            const uint8_t byte = order[at];
            std::copy_backward(order.begin(), order.begin() + static_cast<std::ptrdiff_t>(at),
                               order.begin() + static_cast<std::ptrdiff_t>(at) + 1);
            order[0] = byte;
            transformed.push_back(byte);
        }
        if (!flush_run() || (bit + 7) / 8 != bits_bytes.size())
        {
            return corrupt();
        }
    }
    else if (!bits_bytes.empty())
    {
        return corrupt();
    }
    if (transformed.size() != size)
    {
        return corrupt();
    }
    return inverse_bwt(transformed, primary, out);
}

} // namespace compressor
