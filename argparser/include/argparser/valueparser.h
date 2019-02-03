#ifndef valueparser_h
#define valueparser_h

#include <algorithm>
#include <string>
#include <sstream>

namespace argparser
{

template<typename T>
struct ValueParser
{
    static T parse(const std::string& str) {
        std::istringstream ss(str);
        T ret;
        ss >> ret;
        return ret;
    }
};

template<> bool ValueParser<bool>::parse(const std::string& str);
template<> std::string ValueParser<std::string>::parse(const std::string& str);
template<> int ValueParser<int>::parse(const std::string& str);
template<> float ValueParser<float>::parse(const std::string& str);
template<> double ValueParser<double>::parse(const std::string& str);
template<> uint8_t ValueParser<uint8_t>::parse(const std::string& str);
template<> uint16_t ValueParser<uint16_t>::parse(const std::string& str);
template<> uint32_t ValueParser<uint32_t>::parse(const std::string& str);
template<> uint64_t ValueParser<uint64_t>::parse(const std::string& str);
    
} // argparser

#endif /* valueparser_h */
