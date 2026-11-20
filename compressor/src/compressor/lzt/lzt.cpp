#include <compressor/lzt/lzt.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
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

// Offsets: code 1 + floor(log2(offset)), the bits below the top one as extra bits.
Coded offset_code(uint32_t offset)
{
    const auto top = static_cast<unsigned>(std::bit_width(offset)) - 1;
    return {.code = static_cast<uint8_t>(1 + top),
            .extra_bits = static_cast<uint8_t>(top),
            .extra = offset - (uint32_t{1} << top)};
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
void put_stream(std::vector<uint8_t>& out, std::span<const uint8_t> symbols)
{
    std::vector<uint8_t> coded;
    if (!symbols.empty())
    {
        tans_encode(symbols, coded);
    }
    put_varint(out, coded.size());
    out.insert(out.end(), coded.begin(), coded.end());
}

bool get_stream(std::span<const uint8_t> in, std::size_t& at, std::size_t count,
                std::vector<uint8_t>& symbols)
{
    uint64_t length = 0;
    if (!get_varint(in, at, length) || length > in.size() - at || (count == 0) != (length == 0))
    {
        return false;
    }
    symbols.clear();
    if (count > 0 && !tans_decode(in.subspan(at, static_cast<std::size_t>(length)), count, symbols))
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

std::expected<void, Error> lzt_decode(std::span<const uint8_t> payload, std::vector<uint8_t>& out,
                                      std::size_t size)
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
    if (!get_stream(payload, at, static_cast<std::size_t>(literal_count), literals) ||
        !get_stream(payload, at, count, literal_codes) ||
        !get_stream(payload, at, count, length_codes) ||
        !get_stream(payload, at, count, offset_codes))
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
        if (literal_codes[i] > max_length_code || length_codes[i] > max_length_code ||
            offset_code > max_offset_code ||
            !extra.get(length_extra_bits(literal_codes[i]), literal_extra) ||
            !extra.get(length_extra_bits(length_codes[i]), length_extra) ||
            !extra.get(offset_code == repeat_offset ? 0 : offset_code - 1U, offset_extra))
        {
            return fail();
        }
        const uint64_t run = length_value(literal_codes[i], literal_extra);
        const uint64_t length =
            uint64_t{length_value(length_codes[i], length_extra)} + lz77_min_match;
        uint64_t offset = last_offset;
        if (offset_code != repeat_offset)
        {
            offset = (uint64_t{1} << (offset_code - 1U)) | offset_extra;
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

} // namespace compressor
