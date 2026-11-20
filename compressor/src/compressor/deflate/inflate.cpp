#include <compressor/deflate/inflate.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <vector>

#include <compressor/error.h>

namespace compressor
{

namespace
{

constexpr unsigned table_bits = 15;
constexpr std::size_t max_symbols = 288;

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
constexpr std::array<uint8_t, 19> code_length_order{16, 17, 18, 0, 8,  7, 9,  6, 10, 5,
                                                    11, 4,  12, 3, 13, 2, 14, 1, 15};

std::unexpected<Error> corrupt()
{
    return std::unexpected(Error::corrupt_payload);
}

class BitReader
{
public:
    explicit BitReader(std::span<const uint8_t> bytes)
        : bytes_(bytes)
    {
    }

    // Up to 32 bits, least significant first; false past the end.
    bool take(unsigned count, uint32_t& value)
    {
        refill();
        if (filled_ < count)
        {
            return false;
        }
        value = static_cast<uint32_t>(buffer_ & ((uint64_t{1} << count) - 1));
        buffer_ >>= count;
        filled_ -= count;
        return true;
    }

    // The next table_bits bits, zero past the end, and how many of them are real.
    uint32_t peek(unsigned& available)
    {
        refill();
        available = filled_;
        return static_cast<uint32_t>(buffer_ & ((uint64_t{1} << table_bits) - 1));
    }

    void skip(unsigned count)
    {
        buffer_ >>= count;
        filled_ -= count;
    }

    void align()
    {
        skip(filled_ % 8);
    }

    // After align(): copies `count` whole bytes.
    bool copy(std::size_t count, std::vector<uint8_t>& out)
    {
        while (count > 0 && filled_ >= 8)
        {
            out.push_back(static_cast<uint8_t>(buffer_));
            skip(8);
            --count;
        }
        if (count > bytes_.size() - next_)
        {
            return false;
        }
        out.insert(out.end(), bytes_.begin() + static_cast<std::ptrdiff_t>(next_),
                   bytes_.begin() + static_cast<std::ptrdiff_t>(next_ + count));
        next_ += count;
        return true;
    }

    // Whole bytes not yet read; the bits left in the current byte are padding.
    std::size_t bytes_left() const
    {
        return (bytes_.size() - next_) + (filled_ / 8);
    }

private:
    void refill()
    {
        while (filled_ <= 56 && next_ < bytes_.size())
        {
            buffer_ |= uint64_t{bytes_[next_]} << filled_;
            ++next_;
            filled_ += 8;
        }
    }

    std::span<const uint8_t> bytes_;
    std::size_t next_ = 0;
    uint64_t buffer_ = 0;
    unsigned filled_ = 0;
};

struct Entry
{
    uint16_t symbol = 0;
    uint8_t length = 0; // 0: no code starts with these bits
};

// Indexed by the next table_bits bits, first bit in the lowest position.
class Table
{
public:
    // False for an over-subscribed code, or an incomplete one other than a single code of length 1
    // (which zlib also accepts); all-zero lengths give a table that decodes nothing.
    bool build(std::span<const uint8_t> lengths)
    {
        entries_.assign(std::size_t{1} << table_bits, Entry{});
        std::array<uint32_t, table_bits + 1> count{};
        for (const uint8_t length : lengths)
        {
            ++count[length];
        }
        count[0] = 0;
        int left = 1;
        for (unsigned length = 1; length <= table_bits; ++length)
        {
            left = (left * 2) - static_cast<int>(count[length]);
            if (left < 0)
            {
                return false;
            }
        }
        uint32_t used = 0;
        for (unsigned length = 1; length <= table_bits; ++length)
        {
            used += count[length];
        }
        if (left > 0 && !(used == 1 && count[1] == 1) && used != 0)
        {
            return false;
        }
        std::array<uint32_t, table_bits + 2> next{};
        for (unsigned length = 1; length <= table_bits; ++length)
        {
            next[length + 1] = (next[length] + count[length]) << 1U;
        }
        for (std::size_t symbol = 0; symbol < lengths.size(); ++symbol)
        {
            const unsigned length = lengths[symbol];
            if (length == 0)
            {
                continue;
            }
            const uint32_t code = next[length]++;
            uint32_t reversed = 0;
            for (unsigned bit = 0; bit < length; ++bit)
            {
                reversed |= ((code >> bit) & 1U) << (length - 1 - bit);
            }
            for (uint32_t index = reversed; index < entries_.size(); index += 1U << length)
            {
                entries_[index] = {.symbol = static_cast<uint16_t>(symbol),
                                   .length = static_cast<uint8_t>(length)};
            }
        }
        return true;
    }

    bool decode(BitReader& bits, uint16_t& symbol) const
    {
        unsigned available = 0;
        const Entry entry = entries_[bits.peek(available)];
        if (entry.length == 0 || entry.length > available)
        {
            return false;
        }
        bits.skip(entry.length);
        symbol = entry.symbol;
        return true;
    }

private:
    std::vector<Entry> entries_;
};

void fixed_tables(Table& literals, Table& distances)
{
    std::array<uint8_t, max_symbols> lengths{};
    std::fill(lengths.begin(), lengths.begin() + 144, 8);
    std::fill(lengths.begin() + 144, lengths.begin() + 256, 9);
    std::fill(lengths.begin() + 256, lengths.begin() + 280, 7);
    std::fill(lengths.begin() + 280, lengths.end(), 8);
    literals.build(lengths);
    // 32 five-bit codes make the code complete; 30 and 31 are then rejected when decoded.
    std::array<uint8_t, 32> distance_lengths{};
    distance_lengths.fill(5);
    distances.build(distance_lengths);
}

bool dynamic_tables(BitReader& bits, Table& literals, Table& distances)
{
    uint32_t literal_count = 0;
    uint32_t distance_count = 0;
    uint32_t code_length_count = 0;
    if (!bits.take(5, literal_count) || !bits.take(5, distance_count) ||
        !bits.take(4, code_length_count))
    {
        return false;
    }
    literal_count += 257;
    distance_count += 1;
    code_length_count += 4;
    if (literal_count > 286 || distance_count > 30)
    {
        return false;
    }
    std::array<uint8_t, 19> code_length_lengths{};
    for (uint32_t i = 0; i < code_length_count; ++i)
    {
        uint32_t length = 0;
        if (!bits.take(3, length))
        {
            return false;
        }
        code_length_lengths[code_length_order[i]] = static_cast<uint8_t>(length);
    }
    Table code_lengths;
    if (!code_lengths.build(code_length_lengths))
    {
        return false;
    }

    std::array<uint8_t, 286 + 30> lengths{};
    const uint32_t total = literal_count + distance_count;
    for (uint32_t i = 0; i < total;)
    {
        uint16_t symbol = 0;
        if (!code_lengths.decode(bits, symbol))
        {
            return false;
        }
        if (symbol < 16)
        {
            lengths[i++] = static_cast<uint8_t>(symbol);
            continue;
        }
        uint32_t repeat = 0;
        uint8_t value = 0;
        if (symbol == 16)
        {
            if (i == 0 || !bits.take(2, repeat))
            {
                return false;
            }
            value = lengths[i - 1];
            repeat += 3;
        }
        else if (symbol == 17)
        {
            if (!bits.take(3, repeat))
            {
                return false;
            }
            repeat += 3;
        }
        else
        {
            if (!bits.take(7, repeat))
            {
                return false;
            }
            repeat += 11;
        }
        if (repeat > total - i)
        {
            return false;
        }
        std::fill_n(lengths.begin() + i, repeat, value);
        i += repeat;
    }
    if (lengths[256] == 0)
    {
        return false; // no end-of-block code
    }
    return literals.build(std::span(lengths).first(literal_count)) &&
           distances.build(std::span(lengths).subspan(literal_count, distance_count));
}

bool inflate_codes(BitReader& bits, const Table& literals, const Table& distances,
                   std::vector<uint8_t>& out, std::size_t start, std::size_t end)
{
    while (true)
    {
        uint16_t symbol = 0;
        if (!literals.decode(bits, symbol))
        {
            return false;
        }
        if (symbol < 256)
        {
            if (out.size() >= end)
            {
                return false;
            }
            out.push_back(static_cast<uint8_t>(symbol));
            continue;
        }
        if (symbol == 256)
        {
            return true;
        }
        const std::size_t length_code = symbol - 257U;
        if (length_code >= length_base.size())
        {
            return false;
        }
        uint32_t extra = 0;
        if (!bits.take(length_extra[length_code], extra))
        {
            return false;
        }
        const std::size_t length = length_base[length_code] + extra;
        uint16_t distance_symbol = 0;
        if (!distances.decode(bits, distance_symbol) || distance_symbol >= distance_base.size() ||
            !bits.take(distance_extra[distance_symbol], extra))
        {
            return false;
        }
        const std::size_t distance = distance_base[distance_symbol] + extra;
        if (distance > out.size() - start || length > end - out.size())
        {
            return false;
        }
        // Byte by byte: a match may overlap what it produces.
        for (std::size_t from = out.size() - distance, i = 0; i < length; ++i)
        {
            out.push_back(out[from + i]);
        }
    }
}

} // namespace

std::expected<void, Error> inflate_bytes(std::span<const uint8_t> in, std::vector<uint8_t>& out,
                                         std::size_t size)
{
    const std::size_t start = out.size();
    const std::size_t end = start + size;
    out.reserve(end);
    BitReader bits(in);
    Table literals;
    Table distances;
    bool final = false;
    while (!final)
    {
        uint32_t header = 0;
        if (!bits.take(3, header))
        {
            return corrupt();
        }
        final = (header & 1U) != 0;
        switch (header >> 1U)
        {
        case 0:
        {
            bits.align();
            uint32_t length = 0;
            uint32_t complement = 0;
            if (!bits.take(16, length) || !bits.take(16, complement) ||
                length != (~complement & 0xFFFFU) || length > end - out.size() ||
                !bits.copy(length, out))
            {
                return corrupt();
            }
            break;
        }
        case 1:
            fixed_tables(literals, distances);
            if (!inflate_codes(bits, literals, distances, out, start, end))
            {
                return corrupt();
            }
            break;
        case 2:
            if (!dynamic_tables(bits, literals, distances) ||
                !inflate_codes(bits, literals, distances, out, start, end))
            {
                return corrupt();
            }
            break;
        default:
            return corrupt();
        }
    }
    if (out.size() != end || bits.bytes_left() != 0)
    {
        return corrupt();
    }
    return {};
}

} // namespace compressor
