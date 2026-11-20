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

// Generous for anything a pipeline coder writes; it only bounds what a reader allocates.
uint64_t max_pipeline_payload(uint64_t size)
{
    return size + (size / 8) + 1024;
}

// Every code is at most max_code_length bits.
uint64_t max_huffman_payload(uint64_t size)
{
    return ((size * max_code_length) + 7) / 8;
}

} // namespace

void write_frame_header(std::ostream& ostream, uint8_t block_size_log2, uint16_t flags)
{
    std::string header(magic.begin(), magic.end());
    header.push_back(static_cast<char>(format_version));
    header.push_back(static_cast<char>(block_size_log2));
    put_le<2>(header, flags);
    ostream.write(header.data(), static_cast<std::streamsize>(header.size()));
}

void write_block(std::ostream& ostream, const EncodedBlock& block)
{
    std::string header;
    header.push_back(static_cast<char>(block.method_));
    put_le<4>(header, block.size_);
    put_le<4>(header, encoded_size(block));
    put_le<4>(header, block.crc_);
    if (block.method_ == Method::huffman)
    {
        for (std::size_t i = 0; i < code_lengths_bytes; ++i)
        {
            const auto low = static_cast<unsigned>(block.code_lengths_[2 * i] & 0x0FU);
            const auto high = static_cast<unsigned>(block.code_lengths_[(2 * i) + 1] & 0x0FU);
            header.push_back(static_cast<char>(low | (high << 4U)));
        }
    }
    ostream.write(header.data(), static_cast<std::streamsize>(header.size()));
    write_bytes(ostream, block.payload_);
}

void write_end_block(std::ostream& ostream, uint64_t total_size)
{
    std::string end_block(1, static_cast<char>(end_marker));
    put_le<8>(end_block, total_size);
    ostream.write(end_block.data(), static_cast<std::streamsize>(end_block.size()));
}

uint64_t written_size(const EncodedBlock& block)
{
    return block_header_bytes + encoded_size(block);
}

std::ostream& operator<<(std::ostream& ostream, const EncodedData& data)
{
    write_frame_header(ostream, data.block_size_log2_);
    uint64_t total = 0;
    for (const EncodedBlock& block : data.blocks_)
    {
        write_block(ostream, block);
        total += block.size_;
    }
    write_end_block(ostream, total);
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

std::expected<void, Error> read_block_body(std::istream& istream, uint64_t block_size,
                                           EncodedBlock& block)
{
    std::array<unsigned char, block_header_bytes - 1> fields{};
    if (auto read = read_exactly(istream, fields); !read)
    {
        return read;
    }
    block.size_ = static_cast<uint32_t>(get_le<4>(fields));
    const uint64_t encoded = get_le<4>(std::span(fields).subspan(4));
    block.crc_ = static_cast<uint32_t>(get_le<4>(std::span(fields).subspan(8)));
    block.code_lengths_ = {};

    if (block.size_ == 0 || block.size_ > block_size)
    {
        return std::unexpected(Error::corrupt_header);
    }
    uint64_t payload_bytes = encoded;
    if (block.method_ == Method::stored)
    {
        if (encoded != block.size_)
        {
            return std::unexpected(Error::corrupt_header);
        }
    }
    else if (block.method_ == Method::pipeline)
    {
        if (encoded < pipeline_descriptor_bytes || encoded > max_pipeline_payload(block.size_))
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
            return read;
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
    return read_exactly(istream, block.payload_);
}

} // namespace

FrameReader::FrameReader(std::istream& istream)
    : istream_(istream)
{
}

std::expected<uint8_t, Error> FrameReader::read_header()
{
    std::array<unsigned char, frame_header_bytes> header{};
    istream_.read(reinterpret_cast<char*>(header.data()),
                  static_cast<std::streamsize>(header.size()));
    const auto got = static_cast<std::size_t>(istream_.gcount());
    if (istream_.bad())
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
    if (header[4] < min_format_version || header[4] > format_version)
    {
        return std::unexpected(Error::unsupported_version);
    }
    version_ = header[4];
    const uint8_t block_size_log2 = header[5];
    const auto flags = static_cast<uint16_t>(get_le<2>(std::span(header).subspan(6)));
    if (block_size_log2 < min_block_size_log2 || block_size_log2 > max_block_size_log2 ||
        (flags & ~unsigned{archive_flag | search_index_flag}) != 0U ||
        ((flags & search_index_flag) != 0 && (flags & archive_flag) == 0))
    {
        return std::unexpected(Error::corrupt_header);
    }
    flags_ = flags;
    block_size_ = uint64_t{1} << block_size_log2;
    total_ = 0;
    last_size_.reset();
    return block_size_log2;
}

void FrameReader::start_blocks(uint8_t block_size_log2)
{
    version_ = format_version;
    flags_ = archive_flag;
    block_size_ = uint64_t{1} << block_size_log2;
    total_ = 0;
    last_size_.reset();
}

std::expected<bool, Error> FrameReader::read_block(EncodedBlock& block)
{
    std::array<unsigned char, 1> method{};
    if (auto read = read_exactly(istream_, method); !read)
    {
        return std::unexpected(read.error());
    }
    if (method[0] == end_marker)
    {
        std::array<unsigned char, end_block_bytes - 1> end{};
        if (auto read = read_exactly(istream_, end); !read)
        {
            return std::unexpected(read.error());
        }
        if (get_le<8>(end) != total_ ||
            ((flags_ & archive_flag) == 0 && istream_.peek() != std::istream::traits_type::eof()))
        {
            return std::unexpected(Error::corrupt_header);
        }
        if (istream_.bad())
        {
            return std::unexpected(Error::read_failed);
        }
        return false;
    }
    const auto last_method =
        static_cast<unsigned char>(version_ >= 2 ? Method::pipeline : Method::huffman);
    if (method[0] > last_method)
    {
        return std::unexpected(Error::corrupt_header);
    }
    // Only the last block may be shorter than the block size.
    if (last_size_ && *last_size_ != block_size_)
    {
        return std::unexpected(Error::corrupt_header);
    }
    block.method_ = static_cast<Method>(method[0]);
    if (auto read = read_block_body(istream_, block_size_, block); !read)
    {
        return std::unexpected(read.error());
    }
    last_size_ = block.size_;
    total_ += block.size_;
    return true;
}

uint16_t FrameReader::flags() const
{
    return flags_;
}

uint64_t FrameReader::total_size() const
{
    return total_;
}

std::expected<EncodedData, Error> read_encoded(std::istream& istream)
{
    FrameReader reader(istream);
    auto block_size_log2 = reader.read_header();
    if (!block_size_log2)
    {
        return std::unexpected(block_size_log2.error());
    }
    if (reader.flags() != 0)
    {
        return std::unexpected(Error::unexpected_archive);
    }
    EncodedData data;
    data.block_size_log2_ = *block_size_log2;
    while (true)
    {
        EncodedBlock block;
        auto more = reader.read_block(block);
        if (!more)
        {
            return std::unexpected(more.error());
        }
        if (!*more)
        {
            return data;
        }
        data.blocks_.push_back(std::move(block));
    }
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
