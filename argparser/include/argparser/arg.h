#ifndef arg_h
#define arg_h

#include <string>
#include <memory>
#include <vector>

#include <argparser/basearg.h>
#include <argparser/valueparser.h>

namespace argparser
{

std::string simplify_option(const std::string& argument);

template<typename T>
class Arg : public BaseArg {
public:
    using value_type = T;
    
    explicit Arg(std::string key, std::string arg, std::string short_arg, std::string description, const T default_value)
    : BaseArg(key, arg, short_arg, description)
    , value_(default_value)
    { }
    
    explicit Arg(std::string arg, std::string short_arg, std::string description, const T default_value)
    : BaseArg(simplify_option(arg), arg, short_arg, description)
    , value_(default_value)
    { }

    
    Arg(const Arg& other)
    : BaseArg(other)
    , value_(other.value_)
    { }
    
    virtual ~Arg()
    { }
    
    Arg(Arg&& other) = default;
    Arg& operator=(const Arg& other) = default;
    Arg& operator=(Arg&& other) = default;

    value_type value() const {
        return value_;
    }
    
    void setValue(const T& value) {
        value_ = value;
    }
    
    template <typename P>
    struct parse_impl {
        static P parse(const std::string& str) {
            return ValueParser<P>::parse(str);
        }
    };

    template <typename P>
    struct parse_impl<std::vector<P>> {
        static std::vector<P> parse(const std::string& str) {
            auto value = ValueParser<P>::parse(str);
            return {value};
        }
    };

    void parse(const std::string str) {
        value_ = parse_impl<value_type>::parse(str);
    }

    virtual std::shared_ptr<BaseArg> clone(const std::string& str)
    {
        if (Arg<T> *current = dynamic_cast<Arg<T>*>(this))
        {
            auto new_arg = std::make_shared<Arg<T>>(*current);
            new_arg->parse(str);
            return new_arg;
        }
        else
        {
            return nullptr;
        }            
    }
    
private:
    value_type value_;
};


}
#endif /* arg_h */
