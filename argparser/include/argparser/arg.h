#ifndef arg_h
#define arg_h

#include <string>
#include <memory>

#include <argparser/basearg.h>
#include <argparser/valueparser.h>

namespace argparser
{

template<typename T>
class Arg : public BaseArg {
public:
    using value_type = T;
    
    explicit Arg(const std::string arg, const std::string description, const T default_value)
    : BaseArg(arg, description)
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

    virtual void parse(const std::string& str) {
        value_ = ValueParser<value_type>::parse(str);
    }
    
    virtual std::shared_ptr<BaseArg> clone(const std::string& str)
    {
        if (Arg<T> *current = dynamic_cast<Arg<T>*>(this))
        {
            auto new_arg = std::make_shared<Arg<T>>(*current);
            new_arg->setValue(ValueParser<T>::parse(str));
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
