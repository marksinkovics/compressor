#ifndef argparser_h
#define argparser_h

#include <memory>
#include <map>
#include <string>
#include <vector>

#include <argparser/arg.h>

namespace argparser
{

class Argparser {

public:
    using container_type = std::map<std::string, std::shared_ptr<BaseArg>>;
    
    Argparser(int argc, char** argv);
    
    container_type parse();
    
    template<typename T>
    void add_argument(const std::string& argument, const std::string& description, const T default_value = T())
    {
        auto simplified_argument = simplify_arg_name(argument);
        args_[simplified_argument] = std::make_shared<Arg<T>>(argument, description, default_value);
    }
    
    bool has_argument(const std::string& argument);
    
    void print_help() const;

private:
    void unify_input_arguments(int argc, char** argv);
    static std::string simplify_arg_name(const std::string& argument);

    std::vector<std::string> inputs_;
    
    container_type args_;    
    container_type options_;
    
    std::string program_name_;
};

} // argparser

#endif /* argparser_h */
