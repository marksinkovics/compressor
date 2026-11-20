#ifndef Search_h
#define Search_h

#include <cstdint>
#include <iosfwd>
#include <string>
#include <string_view>
#include <vector>

namespace compressor
{

enum class SearchOutput : uint8_t
{
    lines, // each matching line, with its number
    count, // the number of matching lines per file
    names, // the names of the files with a match
};

struct SearchOptions
{
    std::string pattern; // a fixed string, without '\n'
    SearchOutput output = SearchOutput::lines;
    bool byte_offsets = false; // also print where each matching line starts in its file
};

enum class SearchResult : uint8_t
{
    matched,
    not_matched,
    failed,
};

// Finds the lines that contain the pattern in one file after another, fed in pieces of any size,
// and prints them as `grep -n` does. A file with a NUL byte in a matching line is reported as a
// binary file match instead of printing its lines.
class LineSearcher
{
public:
    LineSearcher(SearchOptions options, std::ostream& out);

    // `names`: the file and any others with the same contents. With `prefix`, every line printed
    // starts with the name; otherwise only names output and binary matches use it.
    void begin(std::vector<std::string> names, bool prefix);
    void feed(std::string_view bytes);
    void end();

    bool matched() const;

private:
    void finish_line();
    // `line` contains the pattern; line_number_ and line_start_ are its own.
    void report_line(std::string_view line);
    void print_prefix(const std::string& name);

    SearchOptions options_;
    std::ostream& out_;
    std::vector<std::string> names_;
    bool prefix_ = false;
    std::string line_; // the start of the current line, carried over from earlier pieces
    uint64_t line_number_ = 1;
    uint64_t line_start_ = 0; // byte offset in the file
    uint64_t position_ = 0;
    uint64_t count_ = 0;
    bool binary_ = false;
    bool matched_ = false; // in any file
};

} // namespace compressor

#endif /* Search_h */
