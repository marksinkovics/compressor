#include <argparser/arg.h>

namespace argparser
{

std::string simplify_option(const std::string& argument)
{
    auto has_prefix = [](std::string str, std::string_view prefix) -> decltype(str.find(prefix) == 0) {
        return str.find(prefix, 0) == 0;
    };
    
    std::string arg = argument;
    while (has_prefix(arg, "-") || has_prefix(arg, "_")) {
        arg.erase(arg.begin());
    }
    
    return arg;
}

}
