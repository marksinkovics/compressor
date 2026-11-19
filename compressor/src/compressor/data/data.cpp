#include <compressor/data/data.h>
#include <compressor/engine/engine.h>

#include <algorithm>
#include <array>
#include <istream>
#include <iterator>
#include <ostream>
#include <utility>
#include <vector>

namespace compressor
{

std::ostream& operator<<(std::ostream& ostream, const DecodedData& data)
{
    std::ostream_iterator<uint8_t> output_iterator(ostream);
    std::ranges::copy(data.data_, output_iterator);
    return ostream;
}

std::istream& operator>>(std::istream& istream, DecodedData& data)
{
    std::vector<uint8_t> input_data((std::istreambuf_iterator<char>(istream)),
                                    std::istreambuf_iterator<char>());
    data.data_ = std::move(input_data);
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

    ostream.write(reinterpret_cast<const char*>(data.payload_.data()),
                  static_cast<std::streamsize>(data.payload_.size()));
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

    result.payload_.assign(std::istreambuf_iterator<char>(istream),
                           std::istreambuf_iterator<char>());

    data = std::move(result);
    return istream;
}

bool operator==(const EncodedData& lhs, const EncodedData& rhs)
{
    return lhs.size_ == rhs.size_ && lhs.code_lengths_ == rhs.code_lengths_ &&
           lhs.payload_ == rhs.payload_;
}

} // namespace compressor
