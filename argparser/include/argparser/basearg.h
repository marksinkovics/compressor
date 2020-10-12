#ifndef base_arg_h
#define base_arg_h

#include <string>

namespace argparser
{

class BaseArg
{
public:
    BaseArg(std::string key, std::string arg, std::string short_arg, std::string description);
    BaseArg(const BaseArg& other);
    
    virtual ~BaseArg();
    
    BaseArg(BaseArg&& other) = default;
    BaseArg& operator=(const BaseArg& other) = default;
    BaseArg& operator=(BaseArg&& other) = default;
    
    virtual std::string key() const;
    virtual std::string arg() const;
    virtual std::string short_arg() const;
    virtual std::string description() const;
    
    virtual bool operator==(const BaseArg& other);
    
    virtual bool operator==(const std::string str);

    virtual bool operator!=(const BaseArg& other);
    
    virtual bool operator<(const BaseArg& other);
    
    virtual std::shared_ptr<BaseArg> clone([[maybe_unused]] const std::string& str);
    
    virtual std::shared_ptr<BaseArg> clone([[maybe_unused]] const std::vector<std::string>& values);
    
private:
    std::string key_;
    std::string arg_;
    std::string short_arg_;
    std::string description_;
};

class ArgMatcher {
private:
    std::string str_;
public:
    ArgMatcher(const std::string str)
    : str_(str)
    {}
    
    bool operator()(std::shared_ptr<BaseArg> arg) const
    {
        return (*arg) == str_;
    }
};

} //argparser

#endif /* base_arg_h */
