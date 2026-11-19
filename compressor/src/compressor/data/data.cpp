#include <compressor/data/data.h>
#include <compressor/engine/engine.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <ios>
#include <istream>
#include <ostream>
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

constexpr std::size_t size_field_bytes = 8;
constexpr std::size_t lengths_field_bytes = symbol_count / 2;

} // namespace

std::ostream& operator<<(std::ostream& ostream, const EncodedData& data)
{
    std::array<char, size_field_bytes + lengths_field_bytes> header{};
    for (std::size_t i = 0; i < size_field_bytes; ++i)
    {
        header[i] = static_cast<char>((data.size_ >> (8 * i)) & 0xFFU);
    }
    for (std::size_t i = 0; i < lengths_field_bytes; ++i)
    {
        const auto low = static_cast<unsigned>(data.code_lengths_[2 * i] & 0x0FU);
        const auto high = static_cast<unsigned>(data.code_lengths_[(2 * i) + 1] & 0x0FU);
        header[size_field_bytes + i] = static_cast<char>(low | (high << 4U));
    }
    ostream.write(header.data(), header.size());

    write_bytes(ostream, data.payload_);
    return ostream;
}

std::istream& operator>>(std::istream& istream, EncodedData& data)
{
    data = EncodedData{};

    std::array<unsigned char, size_field_bytes + lengths_field_bytes> header{};
    istream.read(reinterpret_cast<char*>(header.data()), header.size());
    if (istream.gcount() != static_cast<std::streamsize>(header.size()))
    {
        istream.setstate(std::ios::failbit);
        return istream;
    }

    EncodedData result;
    for (std::size_t i = 0; i < size_field_bytes; ++i)
    {
        result.size_ |= static_cast<uint64_t>(header[i]) << (8 * i);
    }
    bool any_code = false;
    for (std::size_t i = 0; i < lengths_field_bytes; ++i)
    {
        const unsigned char byte = header[size_field_bytes + i];
        result.code_lengths_[2 * i] = byte & 0x0FU;
        result.code_lengths_[(2 * i) + 1] = byte >> 4U;
        any_code = any_code || byte != 0;
    }
    if (!valid_code_lengths(result.code_lengths_) || any_code != (result.size_ > 0))
    {
        istream.setstate(std::ios::failbit);
        return istream;
    }

    read_rest(istream, result.payload_);

    data = std::move(result);
    return istream;
}

bool operator==(const EncodedData& lhs, const EncodedData& rhs)
{
    return lhs.size_ == rhs.size_ && lhs.code_lengths_ == rhs.code_lengths_ &&
           lhs.payload_ == rhs.payload_;
}

} // namespace compressor
