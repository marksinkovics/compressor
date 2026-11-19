#include <compressor/data/data.h>
#include <compressor/engine/engine.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <ios>
#include <istream>
#include <ostream>
#include <span>
#include <streambuf>
#include <string>
#include <utility>
#include <vector>

namespace compressor
{

namespace
{

// Reserves the remaining size when the stream can seek, then reads in blocks, so pipes work too.
void read_rest(std::istream& istream, std::vector<uint8_t>& out)
{
    if (!istream.good())
    {
        return;
    }
    const std::istream::pos_type start = istream.tellg();
    if (start != std::istream::pos_type(-1) && istream.seekg(0, std::ios::end))
    {
        const std::istream::pos_type end = istream.tellg();
        istream.seekg(start);
        if (end > start)
        {
            out.reserve(out.size() + static_cast<std::size_t>(end - start));
        }
    }
    istream.clear();

    constexpr std::size_t block_bytes = std::size_t{1} << 16U;
    std::streambuf* buffer = istream.rdbuf();
    while (true)
    {
        std::size_t room = out.capacity() - out.size();
        if (room == 0)
        {
            // Don't grow a buffer that was reserved exactly.
            if (buffer->sgetc() == std::char_traits<char>::eof())
            {
                break;
            }
            room = block_bytes;
        }
        const std::size_t old_size = out.size();
        out.resize(old_size + room);
        const std::streamsize got = buffer->sgetn(reinterpret_cast<char*>(out.data() + old_size),
                                                  static_cast<std::streamsize>(room));
        out.resize(old_size + static_cast<std::size_t>(got));
        if (std::cmp_less(got, room))
        {
            break;
        }
    }
    istream.setstate(std::ios::eofbit);
}

void write_bytes(std::ostream& ostream, const std::vector<uint8_t>& bytes)
{
    ostream.write(reinterpret_cast<const char*>(bytes.data()),
                  static_cast<std::streamsize>(bytes.size()));
}

} // namespace

std::ostream& operator<<(std::ostream& ostream, const DecodedData& data)
{
    write_bytes(ostream, data.data_);
    return ostream;
}

std::istream& operator>>(std::istream& istream, DecodedData& data)
{
    data.data_.clear();
    read_rest(istream, data.data_);
    return istream;
}

bool operator==(const DecodedData& lhs, const DecodedData& rhs)
{
    return lhs.data_ == rhs.data_;
}

namespace
{

constexpr uint8_t end_marker = 0xFF;

template <std::size_t N> void put_le(std::string& out, uint64_t value)
{
    for (std::size_t i = 0; i < N; ++i)
    {
        out.push_back(static_cast<char>((value >> (8 * i)) & 0xFFU));
    }
}

template <std::size_t N> uint64_t get_le(std::span<const unsigned char> bytes)
{
    uint64_t value = 0;
    for (std::size_t i = 0; i < N; ++i)
    {
        value |= static_cast<uint64_t>(bytes[i]) << (8 * i);
    }
    return value;
}

std::size_t encoded_size(const EncodedBlock& block)
{
    return block.payload_.size() + (block.method_ == Method::huffman ? code_lengths_bytes : 0);
}

// Every code is at most max_code_length bits.
uint64_t max_huffman_payload(uint64_t size)
{
    return ((size * max_code_length) + 7) / 8;
}

} // namespace

std::ostream& operator<<(std::ostream& ostream, const EncodedData& data)
{
    std::string header(magic.begin(), magic.end());
    header.push_back(static_cast<char>(format_version));
    header.push_back(static_cast<char>(data.block_size_log2_));
    put_le<2>(header, 0);
    ostream.write(header.data(), static_cast<std::streamsize>(header.size()));

    uint64_t total = 0;
    for (const EncodedBlock& block : data.blocks_)
    {
        std::string block_header;
        block_header.push_back(static_cast<char>(block.method_));
        put_le<4>(block_header, block.size_);
        put_le<4>(block_header, encoded_size(block));
        put_le<4>(block_header, block.crc_);
        if (block.method_ == Method::huffman)
        {
            for (std::size_t i = 0; i < code_lengths_bytes; ++i)
            {
                const auto low = static_cast<unsigned>(block.code_lengths_[2 * i] & 0x0FU);
                const auto high = static_cast<unsigned>(block.code_lengths_[(2 * i) + 1] & 0x0FU);
                block_header.push_back(static_cast<char>(low | (high << 4U)));
            }
        }
        ostream.write(block_header.data(), static_cast<std::streamsize>(block_header.size()));
        write_bytes(ostream, block.payload_);
        total += block.size_;
    }

    std::string end_block(1, static_cast<char>(end_marker));
    put_le<8>(end_block, total);
    ostream.write(end_block.data(), static_cast<std::streamsize>(end_block.size()));
    return ostream;
}

namespace
{

// Reports a short read as truncated, or as read_failed when the stream broke.
std::expected<void, Error> read_exactly(std::istream& istream, std::span<unsigned char> bytes)
{
    istream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (istream.gcount() == static_cast<std::streamsize>(bytes.size()))
    {
        return {};
    }
    return std::unexpected(istream.bad() ? Error::read_failed : Error::truncated);
}

std::expected<EncodedBlock, Error> read_block(std::istream& istream, Method method,
                                              uint64_t block_size)
{
    std::array<unsigned char, block_header_bytes - 1> fields{};
    if (auto read = read_exactly(istream, fields); !read)
    {
        return std::unexpected(read.error());
    }
    EncodedBlock block;
    block.method_ = method;
    block.size_ = static_cast<uint32_t>(get_le<4>(fields));
    const uint64_t encoded = get_le<4>(std::span(fields).subspan(4));
    block.crc_ = static_cast<uint32_t>(get_le<4>(std::span(fields).subspan(8)));

    if (block.size_ == 0 || block.size_ > block_size)
    {
        return std::unexpected(Error::corrupt_header);
    }
    uint64_t payload_bytes = encoded;
    if (method == Method::stored)
    {
        if (encoded != block.size_)
        {
            return std::unexpected(Error::corrupt_header);
        }
    }
    else
    {
        if (encoded < code_lengths_bytes ||
            encoded - code_lengths_bytes > max_huffman_payload(block.size_))
        {
            return std::unexpected(Error::corrupt_header);
        }
        payload_bytes -= code_lengths_bytes;
        std::array<unsigned char, code_lengths_bytes> lengths{};
        if (auto read = read_exactly(istream, lengths); !read)
        {
            return std::unexpected(read.error());
        }
        for (std::size_t i = 0; i < code_lengths_bytes; ++i)
        {
            block.code_lengths_[2 * i] = lengths[i] & 0x0FU;
            block.code_lengths_[(2 * i) + 1] = lengths[i] >> 4U;
        }
        if (!valid_code_lengths(block.code_lengths_) || block.code_lengths_ == CodeLengths{})
        {
            return std::unexpected(Error::corrupt_header);
        }
    }

    block.payload_.resize(payload_bytes);
    if (auto read = read_exactly(istream, block.payload_); !read)
    {
        return std::unexpected(read.error());
    }
    return block;
}

} // namespace

std::expected<EncodedData, Error> read_encoded(std::istream& istream)
{
    std::array<unsigned char, frame_header_bytes> header{};
    istream.read(reinterpret_cast<char*>(header.data()),
                 static_cast<std::streamsize>(header.size()));
    const auto got = static_cast<std::size_t>(istream.gcount());
    if (istream.bad())
    {
        return std::unexpected(Error::read_failed);
    }
    if (got < magic.size() || !std::equal(magic.begin(), magic.end(), header.begin()))
    {
        return std::unexpected(Error::not_ingot);
    }
    if (got < header.size())
    {
        return std::unexpected(Error::truncated);
    }
    if (header[4] != format_version)
    {
        return std::unexpected(Error::unsupported_version);
    }
    EncodedData data;
    data.block_size_log2_ = header[5];
    if (data.block_size_log2_ < min_block_size_log2 ||
        data.block_size_log2_ > max_block_size_log2 || header[6] != 0 || header[7] != 0)
    {
        return std::unexpected(Error::corrupt_header);
    }
    const uint64_t block_size = uint64_t{1} << data.block_size_log2_;

    uint64_t total = 0;
    while (true)
    {
        std::array<unsigned char, 1> method{};
        if (auto read = read_exactly(istream, method); !read)
        {
            return std::unexpected(read.error());
        }
        if (method[0] == end_marker)
        {
            break;
        }
        if (method[0] > static_cast<unsigned char>(Method::huffman))
        {
            return std::unexpected(Error::corrupt_header);
        }
        // Only the last block may be shorter than the block size.
        if (!data.blocks_.empty() && data.blocks_.back().size_ != block_size)
        {
            return std::unexpected(Error::corrupt_header);
        }
        auto block = read_block(istream, static_cast<Method>(method[0]), block_size);
        if (!block)
        {
            return std::unexpected(block.error());
        }
        total += block->size_;
        data.blocks_.push_back(std::move(*block));
    }

    std::array<unsigned char, end_block_bytes - 1> end{};
    if (auto read = read_exactly(istream, end); !read)
    {
        return std::unexpected(read.error());
    }
    if (get_le<8>(end) != total || istream.peek() != std::istream::traits_type::eof())
    {
        return std::unexpected(Error::corrupt_header);
    }
    if (istream.bad())
    {
        return std::unexpected(Error::read_failed);
    }
    return data;
}

std::istream& operator>>(std::istream& istream, EncodedData& data)
{
    data = EncodedData{};
    auto result = read_encoded(istream);
    if (!result)
    {
        istream.setstate(std::ios::failbit);
        return istream;
    }
    data = std::move(*result);
    return istream;
}

} // namespace compressor
