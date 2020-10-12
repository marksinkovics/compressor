#ifndef argparser_h
#define argparser_h

#include <memory>
#include <map>
#include <string>
#include <vector>

#include <argparser/arg.h>

namespace argparser
{
    
class IArgparser
{
public:
    using input_type = std::vector<std::string>;
    using result_type = std::map<std::string, std::shared_ptr<BaseArg>>;
    using container_type = std::vector<std::shared_ptr<BaseArg>>;
    
    virtual ~IArgparser() = default;

    virtual void add_option(std::shared_ptr<BaseArg> arg) = 0;
    virtual result_type parse() = 0;
    virtual bool has_argument(const std::string& argument) const = 0;
    virtual void print_help() const = 0;
};

class Argparser: public IArgparser
{
public:
    Argparser(int argc, char** argv);
    virtual ~Argparser();
    
    virtual void add_option(std::shared_ptr<BaseArg> arg);
    virtual result_type parse();
    virtual bool has_argument(const std::string& argument) const;
    virtual void print_help() const;
private:
    void unify_input_arguments(int argc, char** argv);

    input_type inputs_;
    container_type options_;
    result_type args_;
    
    std::string program_name_;
};
    
template<typename T>
static std::shared_ptr<BaseArg> CreateArg(const std::string& argument,
                                          const std::string& short_argument,
                                          const std::string& description,
                                          const T default_value = T())
{
    return std::make_shared<Arg<T>>(argument, short_argument, description, default_value);
}

} // argparser

#endif /* argparser_h */
