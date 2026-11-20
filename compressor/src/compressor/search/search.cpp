#include <compressor/search/search.h>

#include <cstdint>
#include <ostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace compressor
{

LineSearcher::LineSearcher(SearchOptions options, std::ostream& out)
    : options_(std::move(options))
    , out_(out)
{
}

void LineSearcher::begin(std::vector<std::string> names, bool prefix)
{
    names_ = std::move(names);
    prefix_ = prefix;
    line_.clear();
    line_number_ = 1;
    line_start_ = 0;
    position_ = 0;
    count_ = 0;
    binary_ = false;
}

void LineSearcher::feed(std::string_view bytes)
{
    while (!bytes.empty())
    {
        const std::size_t newline = bytes.find('\n');
        if (newline == std::string_view::npos)
        {
            line_.append(bytes);
            position_ += bytes.size();
            return;
        }
        line_.append(bytes.substr(0, newline));
        position_ += newline + 1;
        finish_line();
        line_.clear();
        ++line_number_;
        line_start_ = position_;
        bytes.remove_prefix(newline + 1);
    }
}

void LineSearcher::end()
{
    if (!line_.empty())
    {
        finish_line();
    }
    switch (options_.output)
    {
    case SearchOutput::lines:
        break;
    case SearchOutput::count:
        for (const std::string& name : names_)
        {
            print_prefix(name);
            out_ << count_ << '\n';
        }
        break;
    case SearchOutput::names:
        if (count_ > 0)
        {
            for (const std::string& name : names_)
            {
                out_ << name << '\n';
            }
        }
        break;
    }
}

bool LineSearcher::matched() const
{
    return matched_;
}

void LineSearcher::finish_line()
{
    if (line_.find(options_.pattern) == std::string::npos)
    {
        return;
    }
    ++count_;
    matched_ = true;
    if (options_.output != SearchOutput::lines || binary_)
    {
        return;
    }
    if (line_.find('\0') != std::string::npos)
    {
        binary_ = true;
        for (const std::string& name : names_)
        {
            out_ << "Binary file " << name << " matches\n";
        }
        return;
    }
    for (const std::string& name : names_)
    {
        print_prefix(name);
        out_ << line_number_ << ':';
        if (options_.byte_offsets)
        {
            out_ << line_start_ << ':';
        }
        out_ << line_ << '\n';
    }
}

void LineSearcher::print_prefix(const std::string& name)
{
    if (prefix_)
    {
        out_ << name << ':';
    }
}

} // namespace compressor
