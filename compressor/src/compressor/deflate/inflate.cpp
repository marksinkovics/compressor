#include <compressor/deflate/inflate.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <istream>
#include <limits>
#include <optional>
#include <ostream>
#include <span>
#include <utility>
#include <vector>

#include <compressor/checksum/crc32.h>
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
    // Hands over the next stretch of input; empty at the end.
    using Refill = std::function<std::span<const uint8_t>()>;

    explicit BitReader(std::span<const uint8_t> bytes, Refill refill = {})
        : bytes_(bytes)
        , refill_(std::move(refill))
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

    // After align(): hands `count` whole bytes to `sink`.
    template <class Sink> bool copy(std::size_t count, Sink& sink)
    {
        while (count > 0 && filled_ >= 8)
        {
            sink.push(static_cast<uint8_t>(buffer_));
            skip(8);
            --count;
        }
        while (count > 0)
        {
            if (next_ == bytes_.size() && !next_stretch())
            {
                return false;
            }
            const std::size_t take = std::min(count, bytes_.size() - next_);
            sink.append(bytes_.subspan(next_, take));
            next_ += take;
            count -= take;
        }
        return true;
    }

    // Whole bytes not yet read; the bits left in the current byte are padding.
    std::size_t bytes_left() const
    {
        return (bytes_.size() - next_) + (filled_ / 8);
    }

    // After align(): the whole bytes read ahead but not used, then the rest of the current
    // stretch, for whatever follows the stream.
    std::vector<uint8_t> unused()
    {
        std::vector<uint8_t> rest;
        for (; filled_ >= 8; filled_ -= 8, buffer_ >>= 8U)
        {
            rest.push_back(static_cast<uint8_t>(buffer_));
        }
        rest.insert(rest.end(), bytes_.begin() + static_cast<std::ptrdiff_t>(next_), bytes_.end());
        next_ = bytes_.size();
        return rest;
    }

private:
    bool next_stretch()
    {
        if (!refill_)
        {
            return false;
        }
        bytes_ = refill_();
        next_ = 0;
        return !bytes_.empty();
    }

    void refill()
    {
        while (filled_ <= 56)
        {
            if (next_ == bytes_.size() && !next_stretch())
            {
                return;
            }
            buffer_ |= uint64_t{bytes_[next_]} << filled_;
            ++next_;
            filled_ += 8;
        }
    }

    std::span<const uint8_t> bytes_;
    Refill refill_;
    std::size_t next_ = 0;
    uint64_t buffer_ = 0;
    unsigned filled_ = 0;
};

// Output for one block of known size: appends to a vector, never past `end`.
class VectorSink
{
public:
    VectorSink(std::vector<uint8_t>& out, std::size_t size)
        : out_(out)
        , start_(out.size())
        , end_(start_ + size)
    {
        out_.reserve(end_);
    }

    uint64_t produced() const
    {
        return out_.size() - start_;
    }

    std::size_t room() const
    {
        return end_ - out_.size();
    }

    void push(uint8_t byte)
    {
        out_.push_back(byte);
    }

    void append(std::span<const uint8_t> bytes)
    {
        out_.insert(out_.end(), bytes.begin(), bytes.end());
    }

    // Distance already checked against produced().
    void copy_match(std::size_t distance, std::size_t length)
    {
        // Byte by byte: a match may overlap what it produces.
        for (std::size_t from = out_.size() - distance, i = 0; i < length; ++i)
        {
            out_.push_back(out_[from + i]);
        }
    }

    bool complete() const
    {
        return out_.size() == end_;
    }

private:
    std::vector<uint8_t>& out_;
    std::size_t start_;
    std::size_t end_;
};

// Output of unknown size: keeps the last 32 KiB for matches and writes the rest to a stream.
class StreamSink
{
public:
    explicit StreamSink(std::ostream& out)
        : out_(out)
    {
    }

    uint64_t produced() const
    {
        return written_ + buffer_.size();
    }

    static std::size_t room()
    {
        return std::numeric_limits<std::size_t>::max();
    }

    void push(uint8_t byte)
    {
        buffer_.push_back(byte);
        flush_some();
    }

    void append(std::span<const uint8_t> bytes)
    {
        buffer_.insert(buffer_.end(), bytes.begin(), bytes.end());
        flush_some();
    }

    void copy_match(std::size_t distance, std::size_t length)
    {
        for (std::size_t from = buffer_.size() - distance, i = 0; i < length; ++i)
        {
            buffer_.push_back(buffer_[from + i]);
        }
        flush_some();
    }

    // Writes what is left; false when the stream failed.
    bool finish()
    {
        write(buffer_.size());
        return static_cast<bool>(out_);
    }

    uint32_t crc() const
    {
        return crc_;
    }

private:
    static constexpr std::size_t window = std::size_t{32} * 1024;
    static constexpr std::size_t flush_at = window + (std::size_t{1} << 20U);

    void flush_some()
    {
        if (buffer_.size() >= flush_at)
        {
            write(buffer_.size() - window);
        }
    }

    void write(std::size_t count)
    {
        out_.write(reinterpret_cast<const char*>(buffer_.data()),
                   static_cast<std::streamsize>(count));
        crc_ = crc32(std::span(buffer_).first(count), crc_);
        written_ += count;
        buffer_.erase(buffer_.begin(), buffer_.begin() + static_cast<std::ptrdiff_t>(count));
    }

    std::ostream& out_;
    std::vector<uint8_t> buffer_; // the window, then what isn't written yet
    uint64_t written_ = 0;
    uint32_t crc_ = 0;
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

template <class Sink>
bool inflate_codes(BitReader& bits, const Table& literals, const Table& distances, Sink& out)
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
            if (out.room() == 0)
            {
                return false;
            }
            out.push(static_cast<uint8_t>(symbol));
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
        if (distance > out.produced() || length > out.room())
        {
            return false;
        }
        out.copy_match(distance, length);
    }
}

// Every block up to and including the final one.
template <class Sink> bool inflate_blocks(BitReader& bits, Sink& out)
{
    Table literals;
    Table distances;
    bool final = false;
    while (!final)
    {
        uint32_t header = 0;
        if (!bits.take(3, header))
        {
            return false;
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
                length != (~complement & 0xFFFFU) || length > out.room() || !bits.copy(length, out))
            {
                return false;
            }
            break;
        }
        case 1:
            fixed_tables(literals, distances);
            if (!inflate_codes(bits, literals, distances, out))
            {
                return false;
            }
            break;
        case 2:
            if (!dynamic_tables(bits, literals, distances) ||
                !inflate_codes(bits, literals, distances, out))
            {
                return false;
            }
            break;
        default:
            return false;
        }
    }
    return true;
}

} // namespace

std::expected<void, Error> inflate_bytes(std::span<const uint8_t> in, std::vector<uint8_t>& out,
                                         std::size_t size)
{
    BitReader bits(in);
    VectorSink sink(out, size);
    if (!inflate_blocks(bits, sink) || !sink.complete() || bits.bytes_left() != 0)
    {
        return corrupt();
    }
    return {};
}

namespace
{

constexpr unsigned gzip_text = 0x01;
constexpr unsigned gzip_header_crc = 0x02;
constexpr unsigned gzip_extra = 0x04;
constexpr unsigned gzip_name = 0x08;
constexpr unsigned gzip_comment = 0x10;

// The input as stretches for the bit reader, with bytes it handed back read first.
class GzipInput
{
public:
    explicit GzipInput(std::istream& in)
        : in_(in)
        , buffer_(std::size_t{1} << 16U)
    {
    }

    std::span<const uint8_t> next()
    {
        if (!returned_.empty())
        {
            pending_ = std::move(returned_);
            returned_.clear();
            return pending_;
        }
        in_.read(reinterpret_cast<char*>(buffer_.data()),
                 static_cast<std::streamsize>(buffer_.size()));
        bad_ = bad_ || in_.bad();
        return std::span(buffer_).first(static_cast<std::size_t>(in_.gcount()));
    }

    void give_back(std::vector<uint8_t> bytes)
    {
        bytes.insert(bytes.end(), returned_.begin(), returned_.end());
        returned_ = std::move(bytes);
    }

    // One byte, or nothing at the end.
    std::optional<uint8_t> byte()
    {
        while (at_ == current_.size())
        {
            current_ = next();
            at_ = 0;
            if (current_.empty())
            {
                return std::nullopt;
            }
        }
        return current_[at_++];
    }

    // Puts back the byte byte() just returned.
    void unget()
    {
        --at_;
    }

    // What byte() fetched but didn't use goes back before the next stretch.
    void settle()
    {
        give_back({current_.begin() + static_cast<std::ptrdiff_t>(at_), current_.end()});
        current_ = {};
        at_ = 0;
    }

    bool bad() const
    {
        return bad_;
    }

private:
    std::istream& in_;
    std::vector<uint8_t> buffer_;
    std::vector<uint8_t> returned_;
    std::vector<uint8_t> pending_;
    std::span<const uint8_t> current_;
    std::size_t at_ = 0;
    bool bad_ = false;
};

std::expected<void, Error> read_gzip_header(GzipInput& input)
{
    std::vector<uint8_t> header;
    const auto take = [&](std::size_t count) -> bool {
        for (std::size_t i = 0; i < count; ++i)
        {
            const auto byte = input.byte();
            if (!byte)
            {
                return false;
            }
            header.push_back(*byte);
        }
        return true;
    };
    const auto take_string = [&]() -> bool {
        while (true)
        {
            const auto byte = input.byte();
            if (!byte)
            {
                return false;
            }
            header.push_back(*byte);
            if (*byte == 0)
            {
                return true;
            }
        }
    };
    // The magic first, so a short file that isn't gzip says so.
    if (!take(2) || header[0] != 0x1F || header[1] != 0x8B)
    {
        return std::unexpected(Error::not_ingot);
    }
    if (!take(8))
    {
        return std::unexpected(Error::truncated);
    }
    const unsigned flags = header[3];
    if (header[2] != 8 ||
        (flags & ~(gzip_text | gzip_header_crc | gzip_extra | gzip_name | gzip_comment)) != 0)
    {
        return std::unexpected(Error::corrupt_header);
    }
    if ((flags & gzip_extra) != 0)
    {
        if (!take(2))
        {
            return std::unexpected(Error::truncated);
        }
        const std::size_t extra = header[header.size() - 2] | (std::size_t{header.back()} << 8U);
        if (!take(extra))
        {
            return std::unexpected(Error::truncated);
        }
    }
    if (((flags & gzip_name) != 0 && !take_string()) ||
        ((flags & gzip_comment) != 0 && !take_string()))
    {
        return std::unexpected(Error::truncated);
    }
    if ((flags & gzip_header_crc) != 0)
    {
        const uint32_t expected = crc32(header) & 0xFFFFU;
        std::array<uint8_t, 2> stored{};
        for (uint8_t& byte : stored)
        {
            const auto next = input.byte();
            if (!next)
            {
                return std::unexpected(Error::truncated);
            }
            byte = *next;
        }
        if ((stored[0] | (uint32_t{stored[1]} << 8U)) != expected)
        {
            return std::unexpected(Error::checksum_mismatch);
        }
    }
    return {};
}

} // namespace

std::expected<uint64_t, Error> gunzip(std::istream& in, std::ostream& out)
{
    GzipInput input(in);
    uint64_t total = 0;
    for (bool first = true;; first = false)
    {
        if (!first)
        {
            if (!input.byte())
            {
                break; // the end, after at least one member
            }
            input.unget();
        }
        if (auto header = read_gzip_header(input); !header)
        {
            // After a member, anything but another member is damage, not another format.
            return std::unexpected(!first && header.error() == Error::not_ingot
                                       ? Error::corrupt_header
                                       : header.error());
        }
        input.settle();
        BitReader bits({}, [&input] {
            return input.next();
        });
        StreamSink sink(out);
        if (!inflate_blocks(bits, sink))
        {
            return std::unexpected(input.bad() ? Error::read_failed : Error::corrupt_payload);
        }
        if (!sink.finish())
        {
            return std::unexpected(Error::write_failed);
        }
        bits.align();
        input.give_back(bits.unused());
        std::array<uint8_t, 8> trailer{};
        for (uint8_t& byte : trailer)
        {
            const auto next = input.byte();
            if (!next)
            {
                return std::unexpected(Error::truncated);
            }
            byte = *next;
        }
        const uint32_t crc = trailer[0] | (uint32_t{trailer[1]} << 8U) |
                             (uint32_t{trailer[2]} << 16U) | (uint32_t{trailer[3]} << 24U);
        const uint32_t size = trailer[4] | (uint32_t{trailer[5]} << 8U) |
                              (uint32_t{trailer[6]} << 16U) | (uint32_t{trailer[7]} << 24U);
        if (crc != sink.crc())
        {
            return std::unexpected(Error::checksum_mismatch);
        }
        if (size != static_cast<uint32_t>(sink.produced()))
        {
            return std::unexpected(Error::corrupt_payload);
        }
        total += sink.produced();
    }
    if (input.bad())
    {
        return std::unexpected(Error::read_failed);
    }
    return total;
}

} // namespace compressor
