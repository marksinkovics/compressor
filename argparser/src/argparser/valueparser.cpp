#include <argparser/valueparser.h>
#include <string>

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
    
template<> float ValueParser<float>::parse(const std::string& str)
{
    return std::stof(str.c_str());
}

template<> double ValueParser<double>::parse(const std::string& str)
{
    return std::stod(str.c_str());
}

template<> int8_t ValueParser<int8_t>::parse(const std::string& str)
{
    return static_cast<int8_t>(std::stoi(str));
}

template<> int16_t ValueParser<int16_t>::parse(const std::string& str)
{
    return static_cast<int16_t>(std::stoi(str));
}

template<> int32_t ValueParser<int32_t>::parse(const std::string& str)
{
    return static_cast<int32_t>(std::stol(str));
}

template<> int64_t ValueParser<int64_t>::parse(const std::string& str)
{
    return static_cast<int64_t>(std::stoll(str));
}

template<> uint8_t ValueParser<uint8_t>::parse(const std::string& str)
{
    return static_cast<uint8_t>(std::stoul(str));
}

template<> uint16_t ValueParser<uint16_t>::parse(const std::string& str)
{
    return static_cast<uint16_t>(std::stoul(str));
}

template<> uint32_t ValueParser<uint32_t>::parse(const std::string& str)
{
    return static_cast<uint32_t>(std::stoul(str));
}

template<> uint64_t ValueParser<uint64_t>::parse(const std::string& str)
{
    return static_cast<uint64_t>(std::stoull(str));
}

}
