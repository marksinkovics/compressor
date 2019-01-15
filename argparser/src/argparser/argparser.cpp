#include <argparser/argparser.h>

#include <iostream>
#include <sstream>
#include <string>

namespace argparser
{

Argparser::Argparser(int argc, char** argv)
{
    unify_input_arguments(argc, argv);
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

std::map<std::string, std::any> Argparser::parse()
{
    for(std::size_t i = 0; i < inputs_.size(); ++i)
    {
        std::string str = inputs_[i];
        if (str == "-h" || str == "--help")
        {
            print_help();
            return {};
        }
        
        if (arguments_.find(str) != arguments_.end())
        {
            if (auto ptr = std::any_cast<bool>(&arguments_[str]))
            {
                options_[str] = true;
            }
            else if (auto ptr = std::any_cast<std::string>(&arguments_[str]))
            {
                options_[str] = std::string(inputs_[++i]);
            }
            else if (auto ptr = std::any_cast<int>(&arguments_[str]))
            {
                options_[str] = std::stoi(inputs_[++i]);
            }
            else if (auto ptr = std::any_cast<float>(&arguments_[str]))
            {
                options_[str] = std::stof(inputs_[++i]);
            }
            else if (auto ptr = std::any_cast<double>(&arguments_[str]))
            {
                options_[str] = std::stod(inputs_[++i]);
            }
            else
            {
                options_[str] = arguments_[str];
            }
        }
    }
    return options_;
}

bool Argparser::has_argument(const std::string& argument)
{
    return options_.find(argument) != options_.end();
}
    
void Argparser::print_help() const
{
    std::cout << "Usage: " << program_name_ << " [options]\n\n";
    for(auto it = descriptions_.begin(); it != descriptions_.end(); ++it)
    {
        std::cout << "\t" << it->first << "\t\t\t" << it->second << '\n';
    }
    std::cout << "\n\t-h, --help\t\tshow this help message and exit\n";
}
    
}
