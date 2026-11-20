#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include <compressor/error.h>
#include <compressor/fmindex/fmindex.h>

namespace
{

std::string words(std::size_t size, uint32_t seed)
{
    static const std::vector<std::string_view> vocabulary{"index ", "suffix ", "rank ", "sample ",
                                                          "chunk ", "walk, ",  "row\n"};
    std::string text;
    while (text.size() < size)
    {
        seed = (seed * 1103515245U) + 12345U;
        text += vocabulary[(seed >> 16U) % vocabulary.size()];
    }
    text.resize(size);
    return text;
}

std::vector<uint64_t> brute_force(std::string_view data, std::string_view pattern)
{
    std::vector<uint64_t> positions;
    for (std::size_t at = data.find(pattern); at != std::string_view::npos;
         at = data.find(pattern, at + 1))
    {
        positions.push_back(at);
    }
    return positions;
}

compressor::FmIndex build(std::string_view data, std::string* bytes = nullptr)
{
    compressor::FmIndexBuilder builder;
    // In uneven pieces, as the archive writer hands them over.
    for (std::size_t at = 0; at < data.size(); at += 70'001)
    {
        const std::string_view piece = data.substr(at, 70'001);
        builder.add({reinterpret_cast<const uint8_t*>(piece.data()), piece.size()});
    }
    std::ostringstream out;
    builder.write(out, 0);
    const std::string section = out.str();
    if (bytes != nullptr)
    {
        *bytes = section;
    }
    auto owned = std::make_shared<std::string>(section);
    const std::span<const uint8_t> view(reinterpret_cast<const uint8_t*>(owned->data()),
                                        owned->size() - compressor::search_trailer_bytes);
    auto index = compressor::parse_search_index(view, data.size(), owned);
    EXPECT_TRUE(index);
    return index ? *index : compressor::FmIndex{};
}

} // namespace

TEST(FmIndexTests, One_chunk_finds_what_a_plain_search_finds)
{
    const std::string data = words(200'000, 1);
    const compressor::FmIndex index = build(data);
    for (const std::string_view pattern :
         {"i", "row\nrank", "suffix rank sample", "walk, walk, walk", "absent", "\n"})
    {
        EXPECT_EQ(compressor::fm_find(index, pattern), brute_force(data, pattern)) << pattern;
    }
    for (std::size_t at = 0; at + 40 < data.size(); at += 9973)
    {
        const std::string_view pattern = std::string_view(data).substr(at, 1 + (at % 40));
        EXPECT_EQ(compressor::fm_find(index, pattern), brute_force(data, pattern)) << at;
    }
}

TEST(FmIndexTests, Matches_across_chunk_boundaries_are_found_once)
{
    std::string data = words((2U << 20U) + 10'000, 2);
    // Plant a pattern straddling each chunk boundary.
    for (const std::size_t boundary : {std::size_t{1} << 20U, std::size_t{2} << 20U})
    {
        data.replace(boundary - 5, 12, "@@boundary@@");
    }
    const compressor::FmIndex index = build(data);
    EXPECT_EQ(index.chunks.size(), 3U);
    for (const std::string_view pattern : {"@@boundary@@", "sample chunk", "x"})
    {
        EXPECT_EQ(compressor::fm_find(index, pattern), brute_force(data, pattern)) << pattern;
    }
}

TEST(FmIndexTests, Data_just_short_of_a_second_chunk_is_one_chunk)
{
    std::string data = words(compressor::fm_chunk_bytes + compressor::fm_overlap - 1, 3);
    data.replace(data.size() - 8, 8, "the~end!");
    const compressor::FmIndex index = build(data);
    EXPECT_EQ(index.chunks.size(), 1U);
    EXPECT_EQ(compressor::fm_find(index, "the~end!"), brute_force(data, "the~end!"));
}

TEST(FmIndexTests, A_damaged_section_or_a_bad_pattern_is_rejected)
{
    const std::string data = words(10'000, 4);
    std::string section;
    (void)build(data, &section);
    section[20] = static_cast<char>(static_cast<uint8_t>(section[20]) ^ 1U);
    std::istringstream in(section);
    EXPECT_EQ(
        compressor::check_search_index(in, 0, section.size() - compressor::search_trailer_bytes),
        std::unexpected(compressor::Error::corrupt_index));

    const compressor::FmIndex index = build(data);
    EXPECT_FALSE(compressor::fm_find(index, ""));
    EXPECT_FALSE(compressor::fm_find(index, std::string(compressor::fm_overlap + 1, 'a')));
}

TEST(FmIndexTests, Damaged_chunk_contents_fail_cleanly_without_the_crc)
{
    const std::string data = words(20'000, 5);
    std::string section;
    (void)build(data, &section);
    // Every 37th byte of the section damaged in turn and searched, as search skips the CRC.
    for (std::size_t at = 0; at + compressor::search_trailer_bytes < section.size(); at += 37)
    {
        auto damaged = std::make_shared<std::string>(section);
        (*damaged)[at] = static_cast<char>(static_cast<uint8_t>((*damaged)[at]) ^ 0x5AU);
        const std::span<const uint8_t> view(reinterpret_cast<const uint8_t*>(damaged->data()),
                                            damaged->size() - compressor::search_trailer_bytes);
        const auto index = compressor::parse_search_index(view, data.size(), damaged);
        if (index)
        {
            const auto found = compressor::fm_find(*index, "rank sample", 1);
            EXPECT_TRUE(found || found.error() == compressor::Error::corrupt_index) << at;
        }
    }
}
