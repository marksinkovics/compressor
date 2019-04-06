#ifndef Data_h
#define Data_h

#include <map>
#include <vector>
#include <string>
#include <compressor/bitset/bitset.h>

namespace compressor
{
    
struct IData
{
    
};
    
struct DecodedData : IData
{
    DecodedData() {}
    DecodedData(const std::string& input) {
        std::vector<uint8_t> input_data(input.begin(), input.end());
        this->data_ = std::move(input_data);
    }
    std::vector<uint8_t> data_;
};
    
std::ostream& operator<<(std::ostream& ostream, const DecodedData& data);
std::istream& operator>>(std::istream& istream, DecodedData& data);
bool operator==(const DecodedData& lhs, const DecodedData& rhs);
    
struct EncodedData : IData
{
    std::map<bitset, uint8_t> bit_dict_;
    bitset data_;
};
    
std::ostream& operator<<(std::ostream& ostream, const EncodedData& data);
std::istream& operator>>(std::istream& istream, EncodedData& data);
bool operator==(const EncodedData& lhs, const EncodedData& rhs);

}

#endif /* Data_h */

