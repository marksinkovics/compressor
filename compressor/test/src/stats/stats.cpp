#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <system_error>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <compressor/archive/archive.h>
#include <compressor/data/data.h>
#include <compressor/engine/engine.h>
#include <compressor/error.h>
#include <compressor/huffman/huffman.h>
#include <compressor/stats/stats.h>
#include <compressor/stream/encoder.h>

namespace
{

namespace fs = std::filesystem;
using compressor::Error;
using compressor::Frequencies;
using compressor::Method;
using ::testing::HasSubstr;

constexpr uint8_t block_size_log2 = compressor::min_block_size_log2;
constexpr std::size_t block_size = std::size_t{1} << block_size_log2;

std::string encode(const std::string& text)
{
    compressor::BlockEncoder encoder([] {
        return std::make_unique<compressor::Engine>(block_size_log2);
    }, block_size_log2, 1);
    std::istringstream in(text);
    std::ostringstream out;
    EXPECT_TRUE(encoder.encode(in, out));
    return out.str();
}

std::expected<compressor::Stats, Error> stats_of(const std::string& bytes)
{
    std::istringstream in(bytes);
    return compressor::collect_stats(in);
}

// Skewed text over a few letters, so blocks of it are Huffman coded.
std::string letters(std::size_t size)
{
    std::string text(size, 'a');
    for (std::size_t i = 0; i < size; ++i)
    {
        text[i] = "aaaabbc"[(i * i) % 7];
    }
    return text;
}

Frequencies frequencies_of(const std::string& text)
{
    Frequencies frequencies{};
    for (const char c : text)
    {
        ++frequencies[static_cast<uint8_t>(c)];
    }
    return frequencies;
}

} // namespace

TEST(StatsTests, Entropy_is_zero_for_one_symbol_and_one_bit_for_two_equal_ones)
{
    EXPECT_DOUBLE_EQ(compressor::entropy_bits_per_byte(Frequencies{}), 0.0);
    EXPECT_DOUBLE_EQ(compressor::entropy_bits_per_byte(frequencies_of("aaaa")), 0.0);
    EXPECT_DOUBLE_EQ(compressor::entropy_bits_per_byte(frequencies_of("abab")), 1.0);
    EXPECT_DOUBLE_EQ(compressor::entropy_bits_per_byte(frequencies_of("aabc")), 1.5);
}

TEST(StatsTests, Huffman_bits_use_one_code_for_all_the_content)
{
    EXPECT_EQ(compressor::huffman_bits(Frequencies{}), 0U);
    EXPECT_EQ(compressor::huffman_bits(frequencies_of("aaaa")), 4U);
    EXPECT_EQ(compressor::huffman_bits(frequencies_of("aabc")), 6U);
}

TEST(StatsTests, Any_other_file_is_counted_as_it_is)
{
    const auto stats = stats_of("hello");
    ASSERT_TRUE(stats);
    EXPECT_FALSE(stats->ingot);
    EXPECT_EQ(stats->input_bytes, 5U);
    EXPECT_EQ(stats->content_bytes, 5U);
    EXPECT_EQ(stats->frequencies, frequencies_of("hello"));
    EXPECT_TRUE(stats->blocks.empty());

    const auto empty = stats_of("");
    ASSERT_TRUE(empty);
    EXPECT_EQ(empty->content_bytes, 0U);
}

TEST(StatsTests, An_ingot_file_is_decoded_block_by_block)
{
    const std::string text = letters((2 * block_size) + 100);
    const std::string bytes = encode(text);

    const auto stats = stats_of(bytes);
    ASSERT_TRUE(stats);
    EXPECT_TRUE(stats->ingot);
    EXPECT_FALSE(stats->archive);
    EXPECT_EQ(stats->block_size_log2, block_size_log2);
    EXPECT_EQ(stats->input_bytes, bytes.size());
    EXPECT_EQ(stats->content_bytes, text.size());
    EXPECT_EQ(stats->frequencies, frequencies_of(text));
    ASSERT_EQ(stats->blocks.size(), 3U);
    EXPECT_EQ(stats->blocks[0].method, Method::huffman);
    EXPECT_EQ(stats->blocks[2].size, 100U);
    uint64_t encoded = compressor::frame_header_bytes + compressor::end_block_bytes;
    for (const auto& block : stats->blocks)
    {
        encoded += block.encoded_bytes;
    }
    EXPECT_EQ(encoded, bytes.size());
}

TEST(StatsTests, A_folder_archive_counts_its_entries)
{
    const fs::path root = fs::temp_directory_path() / "compressor_stats_archive";
    fs::remove_all(root);
    fs::create_directories(root / "sub");
    std::ofstream(root / "a") << "alpha";
    std::ofstream(root / "sub" / "b") << "beta";
    fs::create_symlink("a", root / "link");

    std::ostringstream warnings;
    auto entries = compressor::scan_tree(root, {}, warnings);
    ASSERT_TRUE(entries);
    compressor::BlockEncoder encoder;
    std::ostringstream out;
    ASSERT_TRUE(compressor::write_archive(encoder, root, *entries, out));
    std::error_code error;
    fs::remove_all(root, error);

    const auto stats = stats_of(out.str());
    ASSERT_TRUE(stats);
    ASSERT_TRUE(stats->archive);
    EXPECT_EQ(stats->archive->files, 2U);
    EXPECT_EQ(stats->archive->directories, 1U);
    EXPECT_EQ(stats->archive->symlinks, 1U);
    EXPECT_EQ(stats->archive->duplicates, 0U);
    EXPECT_EQ(stats->content_bytes, 9U);
}

TEST(StatsTests, Duplicates_are_counted_and_reported)
{
    const fs::path root = fs::temp_directory_path() / "compressor_stats_duplicates";
    fs::remove_all(root);
    fs::create_directories(root);
    for (const char* name : {"a", "b", "c"})
    {
        std::ofstream(root / name) << "same contents";
    }
    std::ostringstream warnings;
    auto entries = compressor::scan_tree(root, {}, warnings);
    ASSERT_TRUE(entries);
    compressor::BlockEncoder encoder;
    std::ostringstream out;
    ASSERT_TRUE(compressor::write_archive(encoder, root, *entries, out));
    std::error_code error;
    fs::remove_all(root, error);

    const auto stats = stats_of(out.str());
    ASSERT_TRUE(stats);
    ASSERT_TRUE(stats->archive);
    EXPECT_EQ(stats->archive->duplicates, 2U);
    EXPECT_EQ(stats->archive->duplicate_bytes, 26U);
    EXPECT_EQ(stats->content_bytes, 13U);
    std::ostringstream report;
    compressor::print_stats(*stats, {}, report);
    EXPECT_THAT(report.str(), HasSubstr("; 2 duplicates of 26 bytes stored once\n"));
}

TEST(StatsTests, A_damaged_ingot_file_fails)
{
    std::string bytes = encode(letters(500));
    bytes.pop_back();

    EXPECT_FALSE(stats_of(bytes));
    EXPECT_EQ(stats_of(bytes.substr(0, 6)).error(), Error::truncated);
}

TEST(StatsTests, The_report_shows_the_input_blocks_content_and_histogram)
{
    compressor::Stats stats;
    stats.input_bytes = 50;
    stats.ingot = true;
    stats.block_size_log2 = 20;
    stats.blocks = {{.method = Method::huffman, .size = 80, .encoded_bytes = 30},
                    {.method = Method::stored, .size = 20, .encoded_bytes = 33}};
    stats.content_bytes = 100;
    stats.frequencies['a'] = 50;
    stats.frequencies['b'] = 25;
    stats.frequencies['\n'] = 25;

    std::ostringstream out;
    compressor::print_stats(stats, {.blocks = true, .histogram = true}, out);
    EXPECT_EQ(out.str(),
              "Input:    50 bytes, ingot file\n"
              "Blocks:   2 of 1 MiB: 1 huffman, 1 stored\n"
              "Ratio:    2.000 (the input is 50.0% of the content)\n"
              "Content:  100 bytes, 3 distinct byte values\n"
              "Entropy:  1.500 bits/byte, so at least 19 bytes for an order-0 coder\n"
              "Huffman:  1.500 bits/byte with one code for everything: 19 bytes plus the code "
              "table\n"
              "\n"
              "Block  Method      Decoded     Encoded   Ratio\n"
              "    0  huffman          80          30   2.667\n"
              "    1  stored           20          33   0.606\n"
              "\n"
              "Byte          Count   Share\n"
              "0x61 'a'         50  50.00%\n"
              "0x0A             25  25.00%\n"
              "0x62 'b'         25  25.00%\n");
}

TEST(StatsTests, The_report_names_a_folder_archive_and_a_plain_file)
{
    compressor::Stats stats;
    stats.input_bytes = 3;
    stats.content_bytes = 3;
    stats.frequencies['x'] = 3;
    std::ostringstream plain;
    compressor::print_stats(stats, {}, plain);
    EXPECT_THAT(plain.str(), HasSubstr("Input:    3 bytes, not an ingot file\n"));

    stats.ingot = true;
    stats.block_size_log2 = 12;
    stats.archive = compressor::ArchiveCounts{.files = 1, .directories = 2, .symlinks = 0};
    std::ostringstream archive;
    compressor::print_stats(stats, {}, archive);
    EXPECT_THAT(archive.str(),
                HasSubstr("ingot folder archive with 1 file, 2 directories and 0 symlinks\n"));
    EXPECT_THAT(archive.str(), HasSubstr("Blocks:   0 of 4 KiB"));
}

TEST(StatsTests, The_graph_is_the_code_tree_of_the_whole_content)
{
    compressor::Stats stats;
    stats.frequencies = frequencies_of("aab");
    std::ostringstream out;
    compressor::write_content_graph(stats, out);
    EXPECT_EQ(out.str(), "digraph G {\n"
                         "\t\"n\" [label=\"ab [3, ]\"];\n"
                         "\t\"n\" -> \"n0\";\n"
                         "\t\"n\" -> \"n1\";\n"
                         "\t\"n0\" [label=\"a [2, 0]\"];\n"
                         "\t\"n1\" [label=\"b [1, 1]\"];\n"
                         "}\n");
}
