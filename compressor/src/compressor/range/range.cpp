#include <compressor/range/range.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdint>
#include <expected>
#include <istream>
#include <optional>
#include <ostream>
#include <streambuf>
#include <string_view>
#include <vector>

#include <compressor/data/data.h>
#include <compressor/error.h>
#include <compressor/stream/decoder.h>

namespace compressor
{

namespace
{

std::optional<uint64_t> parse_number(std::string_view text)
{
    uint64_t value = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end != text.data() + text.size() || text.empty())
    {
        return std::nullopt;
    }
    return value;
}

uint64_t le(std::span<const unsigned char> bytes)
{
    uint64_t value = 0;
    for (std::size_t i = 0; i < bytes.size(); ++i)
    {
        value |= uint64_t{bytes[i]} << (8 * i);
    }
    return value;
}

} // namespace

std::optional<ByteRange> parse_range(std::string_view text)
{
    const auto colon = text.find(':');
    if (colon == std::string_view::npos)
    {
        return std::nullopt;
    }
    const std::string_view from = text.substr(0, colon);
    const std::string_view to = text.substr(colon + 1);
    ByteRange range;
    if (!from.empty())
    {
        const auto value = parse_number(from);
        if (!value)
        {
            return std::nullopt;
        }
        range.from = *value;
    }
    if (!to.empty())
    {
        range.to = parse_number(to);
        if (!range.to || *range.to < range.from)
        {
            return std::nullopt;
        }
    }
    return range;
}

SliceBuf::SliceBuf(std::streambuf* out, uint64_t skip, uint64_t length)
    : out_(out)
    , skip_(skip)
    , length_(length)
{
}

SliceBuf::int_type SliceBuf::overflow(int_type c)
{
    if (traits_type::eq_int_type(c, traits_type::eof()))
    {
        return traits_type::not_eof(c);
    }
    const char byte = traits_type::to_char_type(c);
    return xsputn(&byte, 1) == 1 ? c : traits_type::eof();
}

std::streamsize SliceBuf::xsputn(const char* data, std::streamsize count)
{
    const auto size = static_cast<uint64_t>(count);
    const uint64_t begin = std::max(position_, skip_);
    const uint64_t end = std::min(position_ + size, skip_ + length_);
    if (begin < end && !failed_)
    {
        const auto wanted = static_cast<std::streamsize>(end - begin);
        failed_ = out_->sputn(data + (begin - position_), wanted) != wanted;
    }
    position_ += size;
    return failed_ ? 0 : count;
}

std::expected<BlockTable, Error> read_block_table(std::istream& in)
{
    in.clear();
    in.seekg(0);
    FrameReader reader(in);
    const auto log2 = reader.read_header();
    if (!log2)
    {
        return std::unexpected(log2.error());
    }
    if ((reader.flags() & archive_flag) != 0)
    {
        return std::unexpected(Error::unexpected_archive);
    }
    BlockTable table{.block_size_log2 = *log2,
                     .total_size = 0,
                     .single_block = false,
                     .offsets = {},
                     .flags = reader.flags(),
                     .window_log2 = reader.window_log2()};
    const uint64_t block_size = uint64_t{1} << *log2;
    if ((reader.flags() & single_block_flag) != 0)
    {
        // The size is the varint after the method byte.
        std::array<unsigned char, 1> byte{};
        in.read(reinterpret_cast<char*>(byte.data()), 1);
        for (unsigned shift = 0; in && shift < 35; shift += 7)
        {
            in.read(reinterpret_cast<char*>(byte.data()), 1);
            table.total_size |= uint64_t{byte[0] & 0x7FU} << shift;
            if ((byte[0] & 0x80U) == 0)
            {
                break;
            }
        }
        if (!in || table.total_size == 0 || table.total_size > block_size)
        {
            return std::unexpected(in.bad() ? Error::read_failed : Error::corrupt_header);
        }
        table.single_block = true;
        return table;
    }
    while (true)
    {
        const auto at = static_cast<uint64_t>(in.tellg());
        std::array<unsigned char, block_header_bytes> header{};
        in.read(reinterpret_cast<char*>(header.data()), 1);
        if (!in)
        {
            return std::unexpected(in.bad() ? Error::read_failed : Error::truncated);
        }
        table.offsets.push_back(at);
        if (header[0] == 0xFF)
        {
            std::array<unsigned char, end_block_bytes - 1> total{};
            in.read(reinterpret_cast<char*>(total.data()), total.size());
            if (!in)
            {
                return std::unexpected(in.bad() ? Error::read_failed : Error::truncated);
            }
            table.total_size = le(total);
            break;
        }
        in.read(reinterpret_cast<char*>(header.data() + 1), block_header_bytes - 1);
        if (!in)
        {
            return std::unexpected(in.bad() ? Error::read_failed : Error::truncated);
        }
        const uint64_t encoded = le(std::span(header).subspan(5, 4));
        in.seekg(static_cast<std::streamoff>(at + block_header_bytes + encoded));
    }
    // Every block but the last is full, so the end block's total fixes the block count.
    const uint64_t blocks = table.offsets.size() - 1;
    if (blocks != (table.total_size + block_size - 1) / block_size)
    {
        return std::unexpected(Error::corrupt_header);
    }
    return table;
}

std::expected<void, Error> decode_range(IBlockDecoder& decoder, std::istream& in, ByteRange range,
                                        std::ostream& out)
{
    const auto table = read_block_table(in);
    if (!table)
    {
        return std::unexpected(table.error());
    }
    if (range.from > table->total_size)
    {
        return std::unexpected(Error::bad_range);
    }
    const uint64_t to = std::min(range.to.value_or(table->total_size), table->total_size);
    if (range.from == to)
    {
        return {};
    }
    if (table->single_block)
    {
        in.clear();
        in.seekg(0);
        SliceBuf slice(out.rdbuf(), range.from, to - range.from);
        std::ostream sliced(&slice);
        return decoder.decode(in, sliced);
    }
    const uint8_t log2 = table->block_size_log2;
    const uint64_t first = range.from >> log2;
    const uint64_t last = (to - 1) >> log2;
    SliceBuf slice(out.rdbuf(), range.from - (first << log2), to - range.from);
    std::ostream sliced(&slice);
    const FrameBlocks blocks{.offsets = table->offsets,
                             .block_size_log2 = log2,
                             .flags = table->flags,
                             .window_log2 = table->window_log2};
    return decoder.decode_blocks(in, sliced, blocks, first, last + 1);
}

} // namespace compressor
