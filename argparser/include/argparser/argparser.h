#ifndef argparser_h
#define argparser_h

#include <any>
#include <map>
#include <string>
#include <vector>

namespace argparser
{

class Argparser {

public:
    Argparser(int argc, char** argv);
    /**
     Parse argument to C++ value. It only handles basic types
     e.g. bool, std::string, int, float and double
     @return a map of parser arguments
     */
    std::map<std::string, std::any> parse();
    
    template<typename T>
    void add_argument(const std::string& argument, T default_value, const std::string& description = "") {
        arguments_[argument] = default_value;
        descriptions_[argument] = description;
    }
    bool has_argument(const std::string& argument);
    
    void print_help() const;

private:
    void unify_input_arguments(int argc, char** argv);
    
    std::vector<std::string> inputs_;
    std::map<std::string, std::any> arguments_;
    std::map<std::string, std::string> descriptions_;
    std::map<std::string, std::any> options_;
    
    std::string program_name_;
};

} // argparser

#endif /* argparser_h */
