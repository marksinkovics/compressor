#include <compressor/data/data.h>

namespace Compressor
{

std::ostream& operator<<(std::ostream& ostream, const Data& data)
{
    std::ostream_iterator<uint8_t> output_iterator(ostream);
    std::copy(data.data_.begin(), data.data_.end(), output_iterator);
    return ostream;
}

std::istream& operator>>(std::istream& istream, Data& data)
{
    std::vector<uint8_t> input_data((std::istreambuf_iterator<char>(istream)),
                    std::istreambuf_iterator<char>());
    data.data_ = std::move(input_data);
    return istream;
}

std::ostream& operator<<(std::ostream& ostream, const EncodedData& data)
{
    std::size_t dict_size = data.bit_dict_.size();
    ostream.write(reinterpret_cast<const char*>(&dict_size), sizeof(dict_size));
    for(auto it = data.bit_dict_.begin(); it != data.bit_dict_.end(); ++it)
    {
        ostream << it->first;
        ostream.write(reinterpret_cast<const char*>(&it->second), sizeof(it->second));
    }
    ostream << data.data_;
    return ostream;
}

std::istream& operator>>(std::istream& istream, EncodedData& data)
{
    std::size_t dict_size;
    istream.read(reinterpret_cast<char*>(&dict_size), sizeof(dict_size));
    for (std::size_t i = 0; i < dict_size; ++i)
    {
        BitVector first;
        uint8_t second;
        istream >> first;
        istream.read(reinterpret_cast<char*>(&second), sizeof(second));
        data.bit_dict_[first] = second;
    }
    istream >> data.data_;
    return istream;
}

}
