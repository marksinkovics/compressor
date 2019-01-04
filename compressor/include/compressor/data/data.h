#ifndef Data_h
#define Data_h

#include <map>
#include <vector>
#include <compressor/bitset/bitset.h>

namespace compressor
{

struct Data
{
    std::vector<uint8_t> data_;
};
    
std::ostream& operator<<(std::ostream& ostream, const Data& data);
std::istream& operator>>(std::istream& istream, Data& data);
    
struct EncodedData
{
    std::map<bitset, uint8_t> bit_dict_;
    bitset data_;
};
    
std::ostream& operator<<(std::ostream& ostream, const EncodedData& data);
std::istream& operator>>(std::istream& istream, EncodedData& data);

}

#endif /* Data_h */

