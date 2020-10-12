#include <argparser/argparser.h>

#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <memory>
#include <vector>

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

void Argparser::add_option(std::shared_ptr<BaseArg> arg)
{
    options_.push_back(arg);
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

struct RawArg {
    std::shared_ptr<BaseArg> option;
    std::vector<std::string> values;
};
    
Argparser::result_type Argparser::parse()
{
    std::vector<std::shared_ptr<RawArg>> raw_args;
    
    std::shared_ptr<RawArg> current_raw_arg = nullptr;
    for(std::size_t index = 0; index < inputs_.size(); ++index) {
        std::string str = inputs_[index];
        auto it = std::find_if(std::begin(options_), std::end(options_), ArgMatcher(str));
        if (it != std::end(options_)) {
            if (current_raw_arg != nullptr) {
                raw_args.push_back(current_raw_arg);
            }
            current_raw_arg = std::make_shared<RawArg>();
            current_raw_arg->option = (*it);
        } else {
            if (current_raw_arg != nullptr) {
                current_raw_arg->values.push_back(str);
            }
        }
    }
    
    if (current_raw_arg != nullptr) {
        raw_args.push_back(current_raw_arg);
    }
    
    for (auto raw_arg: raw_args) {
        args_[raw_arg->option->key()] = raw_arg->option->clone(raw_arg->values);
    }
    
    if (args_.find("help") != std::end(args_)) {
        print_help();
        return {};
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
