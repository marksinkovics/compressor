#include <argparser/argparser.h>

#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <memory>

#include <argparser/arg.h>

namespace argparser
{

Argparser::Argparser(int argc, char** argv)
{
    unify_input_arguments(argc, argv);
}
 
Argparser::~Argparser()
{
    
}
    
void Argparser::unify_input_arguments(int argc, char** argv)
{
    // arg[0] is always the name of the program
    program_name_ = argv[0];
    // Skip element 0 and start from 1
    for(int i = 1; i < argc; ++i)
    {
        std::string value = argv[i];
        std::string delimiter = "=";
        auto idx = value.find(delimiter);
        if (idx == std::string::npos)
        {
            inputs_.push_back(value);
        }
        else
        {
            auto start = 0U;
            auto end = value.find(delimiter);
            while (end != std::string::npos)
            {
                inputs_.push_back(value.substr(start, end - start));
                start = end + delimiter.length();
                end = value.find(delimiter, start);
            }
            inputs_.push_back(value.substr(start, end));
        }
    }
}
    
std::string Argparser::simplify_arg_name(const std::string& argument)
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

Argparser::container_type Argparser::parse()
{
    for(std::size_t i = 0; i < inputs_.size(); ++i)
    {
        std::string str = inputs_[i];
        if (str == "-h" || str == "--help")
        {
            print_help();
            return {};
        }
        
        auto simplified_argument = simplify_arg_name(str);
        // arg is exists
        auto arg = args_.find(simplified_argument);
        if(arg != args_.end())
        {
            auto base = arg->second;
            if(Arg<bool> *bool_arg = dynamic_cast<Arg<bool>*>(base.get()))
            {
                auto option = std::make_shared<Arg<bool>>(*bool_arg);
                option->setValue(true);
                options_[simplified_argument] = option;
            }
            else
            {
                options_[simplified_argument] = base->clone(inputs_[++i]);
            }
        }
    }
    return options_;
}
    
bool Argparser::has_argument(const std::string &argument) const
{
    auto symplified_arg = simplify_arg_name(argument);
    return options_.find(symplified_arg) != options_.end();
}
    
void Argparser::print_help() const
{
    auto print_line = [](std::ostream& ostream, const std::string& arg, const std::string& desc) {
        std::ios::fmtflags os_flags (ostream.flags());
        ostream.setf(std::ios::hex);

        ostream << std::setfill(' ') << std::setw(5) << ""
            << std::left << std::setw(10) << arg
            << std::setw(5) << ""
            << std::right << desc << '\n';

        ostream.flags(os_flags);
    };
    
    std::cout << "Usage: " << program_name_ << " [options]\n\n";
    for(auto it = args_.begin(); it != args_.end(); ++it)
    {
        print_line(std::cout, it->second->arg(), it->second->description());
    }
    
    print_line(std::cout, "-h, --help", "Show this help message and exit");
}
    
}
