#ifndef base_arg_h
#define base_arg_h

#include <string>

namespace argparser
{
class BaseArg
{
public:
    BaseArg(std::string arg, std::string description)
    : arg_(arg)
    , description_(description)
    { }
    
    BaseArg(const BaseArg& other)
    : arg_(other.arg_)
    , description_(other.description_)
    { }
    
    virtual ~BaseArg() {}
    
    BaseArg(BaseArg&& other) = default;
    BaseArg& operator=(const BaseArg& other) = default;
    BaseArg& operator=(BaseArg&& other) = default;
    
    virtual std::string arg() const { return arg_; }
    virtual std::string description() const { return description_; }
    
    virtual bool operator==(const BaseArg& other)
    {
        return (arg_ == other.arg_);
    }
    
    virtual bool operator!=(const BaseArg& other)
    {
        return !(*this == other);
    }
    
    virtual bool operator<(const BaseArg& other)
    {
        return this->arg_ < other.arg_;
    }
    
    virtual void parse(__attribute__((unused)) const std::string& str)
    {
        
    }
    
    virtual std::shared_ptr<BaseArg> clone(__attribute__((unused)) const std::string& str)
    {
        return nullptr;
    }
    
private:
    std::string arg_;
    std::string description_;
};

} //argparser

#endif /* base_arg_h */
