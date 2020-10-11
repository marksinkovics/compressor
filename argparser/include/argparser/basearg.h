#ifndef base_arg_h
#define base_arg_h

#include <string>

namespace argparser
{
class BaseArg
{
public:
    BaseArg(std::string key, std::string arg, std::string short_arg, std::string description)
    : key_(key)
    , arg_(arg)
    , short_arg_(short_arg)
    , description_(description)
    { }
    
    BaseArg(const BaseArg& other)
    : key_(other.key_)
    , arg_(other.arg_)
    , short_arg_(other.short_arg_)
    , description_(other.description_)
    { }
    
    virtual ~BaseArg() {}
    
    BaseArg(BaseArg&& other) = default;
    BaseArg& operator=(const BaseArg& other) = default;
    BaseArg& operator=(BaseArg&& other) = default;
    
    virtual std::string key() const { return key_; }
    virtual std::string arg() const { return arg_; }
    virtual std::string short_arg() const { return short_arg_; }
    virtual std::string description() const { return description_; }
    
    virtual bool operator==(const BaseArg& other)
    {
        return (key_ == other.key_
                && arg_ == other.arg_
                && short_arg_ == other.short_arg_);
    }
    
    virtual bool operator!=(const BaseArg& other)
    {
        return !(*this == other);
    }
    
    virtual bool operator<(const BaseArg& other)
    {
        return this->arg_ < other.arg_;
    }
    
    virtual void parse([[maybe_unused]] const std::string& str)
    {
        
    }
    
    virtual std::shared_ptr<BaseArg> clone([[maybe_unused]] const std::string& str)
    {
        return nullptr;
    }
    
    virtual bool is_match(const std::string str) {
        return (key_ == str
                || arg_ == str
                || short_arg_ == str);        
    }
    
private:
    std::string key_;
    std::string arg_;
    std::string short_arg_;
    std::string description_;
};

} //argparser

#endif /* base_arg_h */
