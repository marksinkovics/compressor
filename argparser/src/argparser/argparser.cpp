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
    add_option(CreateArg("--help", "-h", "Show this help message and exit", false));
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
    
Argparser::result_type Argparser::parse()
{    
    for(std::size_t i = 0; i < inputs_.size(); ++i)
    {
        std::string str = inputs_[i];
        
        auto it = std::find_if( options_.begin(), options_.end(), ArgMatcher(str));
        
        if (it == std::end(options_)) {
            continue;
        }
        
        if ((*it)->key() == "help") {
            print_help();
            return {};
        }
        
        if(Arg<bool> *bool_arg = dynamic_cast<Arg<bool>*>(it->get()))
        {
            auto option = std::make_shared<Arg<bool>>(*bool_arg);
            option->setValue(true);
            args_[bool_arg->key()] = option;
        }
        else
        {
            args_[(*it)->key()] = (*it)->clone(inputs_[++i]);
        }
        
    }
    return args_;
}
    
bool Argparser::has_argument(const std::string &argument) const
{
    auto symplified_arg = simplify_option(argument);
    return args_.find(symplified_arg) != args_.end();
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
    for(auto it = options_.begin(); it != options_.end(); ++it)
    {
        print_line(std::cout, (*it)->arg(), (*it)->description());
    }
}
    
}
