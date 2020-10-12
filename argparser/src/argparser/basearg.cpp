#include <argparser/basearg.h>

namespace argparser
{

BaseArg::BaseArg(std::string key, std::string arg, std::string short_arg, std::string description)
: key_(key)
, arg_(arg)
, short_arg_(short_arg)
, description_(description)
{ }

BaseArg::BaseArg(const BaseArg& other)
: key_(other.key_)
, arg_(other.arg_)
, short_arg_(other.short_arg_)
, description_(other.description_)
{ }

BaseArg::~BaseArg() {}

std::string BaseArg::key() const { return key_; }
std::string BaseArg::arg() const { return arg_; }
std::string BaseArg::short_arg() const { return short_arg_; }
std::string BaseArg::description() const { return description_; }

bool BaseArg::operator==(const BaseArg& other)
{
    return (key_ == other.key_
            && arg_ == other.arg_
            && short_arg_ == other.short_arg_);
}

bool BaseArg::operator==(const std::string str)
{
    return (key_ == str
            || arg_ == str
            || short_arg_ == str);
}

bool BaseArg::operator!=(const BaseArg& other)
{
    return !(*this == other);
}

bool BaseArg::operator<(const BaseArg& other)
{
    return this->arg_ < other.arg_;
}

std::shared_ptr<BaseArg> BaseArg::clone([[maybe_unused]] const std::string& str)
{
    return nullptr;
}

std::shared_ptr<BaseArg> BaseArg::clone([[maybe_unused]] const std::vector<std::string>& values)
{
    return nullptr;
}

}
