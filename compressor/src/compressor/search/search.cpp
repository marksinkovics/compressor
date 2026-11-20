#include <compressor/search/search.h>

#include <algorithm>
#include <cstddef>
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
    if (!line_.empty())
    {
        // The line carried over from earlier pieces, if this piece ends it.
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

    // Whole lines: look for the pattern first, and for line boundaries only around a match, so
    // most bytes are only counted for newlines.
    const std::size_t last_newline = bytes.rfind('\n');
    const std::string_view lines = last_newline == std::string_view::npos
                                       ? std::string_view{}
                                       : bytes.substr(0, last_newline + 1);
    const auto newlines = [&lines](std::size_t from, std::size_t to) {
        return static_cast<uint64_t>(std::count(lines.data() + from, lines.data() + to, '\n'));
    };
    std::size_t cursor = 0; // where a line starts
    while (cursor < lines.size())
    {
        const std::size_t match = lines.find(options_.pattern, cursor);
        if (match == std::string_view::npos)
        {
            break;
        }
        std::size_t start = cursor;
        if (match > cursor)
        {
            const std::size_t before = lines.rfind('\n', match - 1);
            if (before != std::string_view::npos && before >= cursor)
            {
                start = before + 1;
            }
        }
        const std::size_t end = lines.find('\n', match);
        line_number_ += newlines(cursor, start);
        line_start_ = position_ + start;
        report_line(lines.substr(start, end - start));
        ++line_number_;
        cursor = end + 1;
    }
    line_number_ += newlines(cursor, lines.size());
    position_ += lines.size();
    line_start_ = position_;
    line_.assign(bytes.substr(lines.size()));
    position_ += line_.size();
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
    if (line_.find(options_.pattern) != std::string::npos)
    {
        report_line(line_);
    }
}

void LineSearcher::report_line(std::string_view line)
{
    ++count_;
    matched_ = true;
    if (options_.output != SearchOutput::lines || binary_)
    {
        return;
    }
    if (line.find('\0') != std::string_view::npos)
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
        out_ << line << '\n';
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
