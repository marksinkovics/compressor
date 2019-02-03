#include <argparser/valueparser.h>

namespace argparser
{

template<> bool ValueParser<bool>::parse(const std::string& str)
{
    std::string str_data = str;
    std::transform(str_data.begin(), str_data.end(), str_data.begin(), ::tolower);
    if (str_data == "true" || str_data == "t" || str_data == "yes" || str_data == "y")
    {
        return true;
    }
    
    if (str_data == "false" || str_data == "f" || str_data == "no" || str_data == "n")
    {
        return false;
    }
    
    return static_cast<int>(std::atoi(str.c_str())) > 0;
}

template<> std::string ValueParser<std::string>::parse(const std::string& str)
{
    return str;
}

template<> int ValueParser<int>::parse(const std::string& str)
{
    return std::atoi(str.c_str());
}
    
template<> float ValueParser<float>::parse(const std::string& str)
{
    return std::stof(str.c_str());
}

template<> double ValueParser<double>::parse(const std::string& str)
{
    return std::stod(str.c_str());
}

template<> uint8_t ValueParser<uint8_t>::parse(const std::string& str)
{
    return static_cast<uint8_t>(std::atoi(str.c_str()));
}

template<> uint16_t ValueParser<uint16_t>::parse(const std::string& str)
{
    return static_cast<uint16_t>(std::atoi(str.c_str()));
}

template<> uint32_t ValueParser<uint32_t>::parse(const std::string& str)
{
    return static_cast<uint32_t>(std::atoi(str.c_str()));
}

template<> uint64_t ValueParser<uint64_t>::parse(const std::string& str)
{
    return static_cast<uint64_t>(std::atoi(str.c_str()));
}

}
