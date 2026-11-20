#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include <compressor/search/search.h>

namespace
{

using compressor::SearchOptions;
using compressor::SearchOutput;

// Feeds `text` as one file named "f", in pieces of `piece` bytes (all at once for 0).
std::string search(const SearchOptions& options, std::string_view text, std::size_t piece = 0,
                   bool prefix = false)
{
    std::ostringstream out;
    compressor::LineSearcher searcher(options, out);
    searcher.begin({"f"}, prefix);
    if (piece == 0)
    {
        searcher.feed(text);
    }
    for (std::size_t at = 0; piece > 0 && at < text.size(); at += piece)
    {
        searcher.feed(text.substr(at, piece));
    }
    searcher.end();
    return out.str();
}

SearchOptions find(std::string pattern, SearchOutput output = SearchOutput::lines)
{
    return {.pattern = std::move(pattern), .output = output, .byte_offsets = false};
}

constexpr std::string_view text = "alpha\nbeta\n\ngamma alpha\nalphabet";

} // namespace

TEST(SearchTests, Matching_lines_are_printed_with_their_numbers)
{
    EXPECT_EQ(search(find("alpha"), text), "1:alpha\n4:gamma alpha\n5:alphabet\n");
    EXPECT_EQ(search(find("zeta"), text), "");
}

TEST(SearchTests, The_pieces_can_split_lines_and_the_pattern_anywhere)
{
    const std::string whole = search(find("alpha"), text);
    for (std::size_t piece = 1; piece <= text.size(); ++piece)
    {
        EXPECT_EQ(search(find("alpha"), text, piece), whole) << piece;
    }
}

TEST(SearchTests, Byte_offsets_are_where_each_line_starts)
{
    SearchOptions options = find("alpha");
    options.byte_offsets = true;
    EXPECT_EQ(search(options, text, 3), "1:0:alpha\n4:12:gamma alpha\n5:24:alphabet\n");
}

TEST(SearchTests, Counts_and_names_are_printed_per_file)
{
    EXPECT_EQ(search(find("alpha", SearchOutput::count), text), "3\n");
    EXPECT_EQ(search(find("alpha", SearchOutput::count), text, 0, true), "f:3\n");
    EXPECT_EQ(search(find("zeta", SearchOutput::count), text, 0, true), "f:0\n");
    EXPECT_EQ(search(find("alpha", SearchOutput::names), text), "f\n");
    EXPECT_EQ(search(find("zeta", SearchOutput::names), text), "");
}

TEST(SearchTests, Every_name_of_shared_contents_is_reported)
{
    std::ostringstream out;
    compressor::LineSearcher searcher(find("b"), out);
    searcher.begin({"x", "y"}, true);
    searcher.feed("a\nb\n");
    searcher.end();
    EXPECT_EQ(out.str(), "x:2:b\ny:2:b\n");
}

TEST(SearchTests, A_binary_match_is_reported_once_per_file)
{
    const std::string binary("ab\0c\nab\n", 8);
    EXPECT_EQ(search(find("ab"), binary), "Binary file f matches\n");
    EXPECT_EQ(search(find("ab", SearchOutput::count), binary), "2\n");
}

TEST(SearchTests, An_empty_pattern_matches_every_line)
{
    EXPECT_EQ(search(find("", SearchOutput::count), "a\n\nb"), "3\n");
}

TEST(SearchTests, A_match_in_any_file_counts)
{
    std::ostringstream out;
    compressor::LineSearcher searcher(find("x"), out);
    searcher.begin({"a"}, true);
    searcher.feed("x\n");
    searcher.end();
    searcher.begin({"b"}, true);
    searcher.feed("y\n");
    searcher.end();
    EXPECT_TRUE(searcher.matched());
    EXPECT_EQ(out.str(), "a:1:x\n");
}
