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
    using container_type = std::map<std::string, std::shared_ptr<BaseArg>>;

    virtual ~IArgparser() = default;

    virtual void add_argument(std::shared_ptr<BaseArg> arg) = 0;
    virtual container_type parse() = 0;
    virtual bool has_argument(const std::string& argument) const = 0;
    virtual void print_help() const = 0;
};

class Argparser: public IArgparser
{
public:
    Argparser(int argc, char** argv);
    virtual ~Argparser();
    
    virtual void add_argument(std::shared_ptr<BaseArg> arg)
    {
        auto simplified_argument = simplify_arg_name(arg->arg());
        args_[simplified_argument] = arg;
    }

    virtual container_type parse();
    virtual bool has_argument(const std::string& argument) const;
    virtual void print_help() const;
private:
    void unify_input_arguments(int argc, char** argv);
    static std::string simplify_arg_name(const std::string& argument);

    std::vector<std::string> inputs_;
    
    container_type args_;    
    container_type options_;
    
    std::string program_name_;
};
    
template<typename T>
static std::shared_ptr<BaseArg> CreateArg(const std::string& argument,
                                          const std::string& description,
                                          const T default_value = T())
{
    return std::make_shared<Arg<T>>(argument, description, default_value);
}

} // argparser

#endif /* argparser_h */
