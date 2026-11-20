#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include <compressor/archive/index.h>
#include <compressor/checksum/crc32.h>
#include <compressor/data/data.h>
#include <compressor/engine/engine.h>
#include <compressor/error.h>
#include <compressor/stream/encoder.h>

namespace
{

using compressor::ArchiveIndex;
using compressor::Entry;
using compressor::EntryType;
using compressor::Error;

constexpr uint8_t block_size_log2 = compressor::min_block_size_log2;
constexpr std::size_t block_size = std::size_t{1} << block_size_log2;

uint32_t crc(const std::string& text)
{
    return compressor::crc32({reinterpret_cast<const uint8_t*>(text.data()), text.size()});
}

Entry dir_entry(std::string path)
{
    return {.type = EntryType::directory, .path = std::move(path), .mode = 0755, .mtime_ns = 7};
}

Entry file_entry(std::string path, uint64_t offset, const std::string& bytes)
{
    return {.type = EntryType::file,
            .path = std::move(path),
            .mode = 0644,
            .mtime_ns = -1'500'000'000,
            .offset = offset,
            .size = bytes.size(),
            .crc = crc(bytes)};
}

Entry link_entry(std::string path, std::string target)
{
    return {.type = EntryType::symlink,
            .path = std::move(path),
            .mode = 0777,
            .mtime_ns = 1,
            .target = std::move(target)};
}

struct Archive
{
    std::string bytes;
    ArchiveIndex index;
    uint64_t index_offset = 0;
};

// A real archive of `data`, with `entries` as its index; the block table comes from the encoder.
Archive make_archive(const std::string& data, std::vector<Entry> entries)
{
    compressor::BlockEncoder encoder(std::make_unique<compressor::Engine>(block_size_log2),
                                     block_size_log2);
    std::istringstream in(data);
    std::ostringstream out;
    auto layout = encoder.encode_frame(in, out, compressor::archive_flag);
    EXPECT_TRUE(layout);
    Archive archive{
        .bytes = {},
        .index = {.block_offsets = layout->block_offsets, .entries = std::move(entries)},
        .index_offset = layout->size};
    compressor::write_index(out, archive.index, archive.index_offset);
    archive.bytes = out.str();
    return archive;
}

std::expected<compressor::ArchiveInfo, Error> read(const std::string& bytes)
{
    std::istringstream in(bytes);
    return compressor::read_archive_index(in);
}

// Empty when the archive reads.
std::optional<Error> read_error(const std::string& bytes)
{
    auto info = read(bytes);
    return info ? std::nullopt : std::optional(info.error());
}

constexpr std::string_view first_text = "hello ";

std::string first()
{
    std::string text(first_text);
    return text;
}

std::string second()
{
    std::string text(block_size + 10, 'x');
    return text;
}

std::vector<Entry> sample_entries()
{
    return {
        dir_entry("docs"),
        file_entry("docs/a.txt", 0, first()),
        dir_entry("docs/empty"),
        file_entry("docs/zero", first().size(), ""),
        link_entry("link", "docs/a.txt"),
        file_entry("z.bin", first().size(), second()),
    };
}

// Only the entry rules: `entries` over `total` bytes, with a plausible block table.
std::optional<Error> entry_error(const std::vector<Entry>& entries, uint64_t total = 0)
{
    constexpr uint64_t encoded_block = 100;
    std::vector<uint64_t> blocks;
    for (uint64_t at = 0; at < total; at += block_size)
    {
        blocks.push_back(compressor::frame_header_bytes + (at / block_size * encoded_block));
    }
    const uint64_t end =
        blocks.empty() ? compressor::frame_header_bytes : blocks.back() + encoded_block;
    auto valid = compressor::validate_index({.block_offsets = blocks, .entries = entries},
                                            block_size_log2, total, end);
    return valid ? std::nullopt : std::optional(valid.error());
}

} // namespace

TEST(ArchiveIndexTests, The_index_round_trips_through_an_archive)
{
    const Archive archive = make_archive(first() + second(), sample_entries());

    const auto info = read(archive.bytes);
    ASSERT_TRUE(info) << compressor::describe(info.error());
    EXPECT_EQ(info->block_size_log2, block_size_log2);
    EXPECT_EQ(info->total_size, first().size() + second().size());
    EXPECT_EQ(info->index_offset, archive.index_offset);
    EXPECT_EQ(info->index, archive.index);
    EXPECT_EQ(info->index.block_offsets.size(), 2U);
    EXPECT_EQ(info->index.block_offsets.front(), compressor::frame_header_bytes);
}

TEST(ArchiveIndexTests, An_empty_archive_has_no_blocks_and_no_entries)
{
    const Archive archive = make_archive("", {});

    const auto info = read(archive.bytes);
    ASSERT_TRUE(info);
    EXPECT_EQ(info->index, ArchiveIndex{});
    EXPECT_EQ(archive.bytes.size(), compressor::frame_header_bytes + compressor::end_block_bytes +
                                        4 + compressor::trailer_bytes);
}

TEST(ArchiveIndexTests, Invalid_paths_are_rejected)
{
    for (const std::string& path : std::vector<std::string>{"", "/abs", "a//b", "./a", "a/.", "..",
                                                            "a/../b", "a/", std::string("x\0y", 3)})
    {
        EXPECT_EQ((entry_error({dir_entry("a"), link_entry(path, "t")})), Error::corrupt_index)
            << '"' << path << '"';
    }
    EXPECT_EQ((entry_error({dir_entry("a"), link_entry("a/.hidden", "t")})), std::nullopt);
}

TEST(ArchiveIndexTests, Duplicate_paths_are_rejected)
{
    EXPECT_EQ((entry_error({dir_entry("a"), link_entry("a", "t")})), Error::corrupt_index);
}

TEST(ArchiveIndexTests, Every_entry_needs_an_earlier_parent_directory)
{
    EXPECT_EQ((entry_error({link_entry("a/b", "t")})), Error::corrupt_index);
    EXPECT_EQ((entry_error({link_entry("a/b", "t"), dir_entry("a")})), Error::corrupt_index);
    // A file or symlink as a parent would let extraction write through it.
    EXPECT_EQ((entry_error({link_entry("a", "/etc"), link_entry("a/b", "t")})),
              Error::corrupt_index);
    EXPECT_EQ((entry_error({file_entry("a", 0, ""), link_entry("a/b", "t")})),
              Error::corrupt_index);
    EXPECT_EQ((entry_error({dir_entry("a"), dir_entry("a/b"), link_entry("a/b/c", "t")})),
              std::nullopt);
}

TEST(ArchiveIndexTests, Files_cover_the_data_in_order)
{
    EXPECT_EQ((entry_error({file_entry("a", 0, "abc"), file_entry("b", 3, "de")}, 5)),
              std::nullopt);
    EXPECT_EQ((entry_error({file_entry("a", 0, "abc"), file_entry("b", 4, "d")}, 5)),
              Error::corrupt_index);
    EXPECT_EQ((entry_error({file_entry("a", 0, "abc"), file_entry("b", 2, "de")}, 5)),
              Error::corrupt_index);
    EXPECT_EQ((entry_error({file_entry("a", 0, "abc")}, 5)), Error::corrupt_index);
    EXPECT_EQ((entry_error({file_entry("a", 0, "abcdef")}, 5)), Error::corrupt_index);
}

TEST(ArchiveIndexTests, Out_of_range_fields_are_rejected)
{
    Entry mode = dir_entry("a");
    mode.mode = 010000;
    EXPECT_EQ((entry_error({mode})), Error::corrupt_index);

    EXPECT_EQ((entry_error({link_entry("a", "")})), Error::corrupt_index);
}

TEST(ArchiveIndexTests, The_block_table_must_match_the_blocks)
{
    const std::vector<Entry> entries{file_entry("a", 0, std::string(block_size + 1, 'x'))};
    const auto check = [&](std::vector<uint64_t> blocks) {
        return compressor::validate_index({.block_offsets = std::move(blocks), .entries = entries},
                                          block_size_log2, block_size + 1, 500);
    };
    EXPECT_TRUE(check({8, 100}));
    EXPECT_EQ(check({8}), std::unexpected(Error::corrupt_index));
    EXPECT_EQ(check({8, 100, 200}), std::unexpected(Error::corrupt_index));
    EXPECT_EQ(check({9, 100}), std::unexpected(Error::corrupt_index));
    EXPECT_EQ(check({8, 8}), std::unexpected(Error::corrupt_index));
    EXPECT_EQ(check({8, 490}), std::unexpected(Error::corrupt_index));
}

TEST(ArchiveIndexTests, Damaged_index_bytes_are_rejected)
{
    const Archive archive = make_archive(first() + second(), sample_entries());
    const std::size_t size = archive.bytes.size();
    const auto with = [&](std::size_t offset, char value) {
        std::string bytes = archive.bytes;
        bytes[offset] = value;
        return bytes;
    };

    // A path byte: only the index CRC catches it.
    EXPECT_EQ(read_error(with(archive.index_offset + 40, 'Q')), Error::corrupt_index);
    EXPECT_EQ(read_error(with(size - 1, 'Z')), Error::corrupt_index);
    EXPECT_EQ(read_error(with(size - compressor::trailer_bytes,
                              static_cast<char>(archive.index_offset + 1))),
              Error::corrupt_index);
    EXPECT_EQ(read_error(with(size - compressor::trailer_bytes + 7, '\x01')), Error::corrupt_index);
    EXPECT_EQ(read_error(archive.bytes.substr(0, size - 1)), Error::corrupt_index);
    EXPECT_EQ(read_error(archive.bytes.substr(0, 20)), Error::truncated);
    EXPECT_EQ(read_error("not an archive at all, but long enough"), Error::not_ingot);
}

TEST(ArchiveIndexTests, A_plain_container_is_not_an_archive)
{
    compressor::BlockEncoder encoder(std::make_unique<compressor::Engine>(block_size_log2),
                                     block_size_log2);
    std::istringstream in(first());
    std::ostringstream out;
    ASSERT_TRUE(encoder.encode(in, out));

    EXPECT_EQ(read_error(out.str() + std::string(compressor::trailer_bytes, '\0')),
              Error::corrupt_index);
}

TEST(ArchiveIndexTests, An_unknown_entry_type_is_rejected)
{
    Archive archive = make_archive("", {dir_entry("a")});
    // The first entry's type byte: after the 4-byte entry count, as there are no blocks.
    archive.bytes[archive.index_offset + 4] = '\x03';
    const std::size_t crc_at = archive.bytes.size() - compressor::trailer_bytes + 8;
    const std::string index =
        archive.bytes.substr(archive.index_offset, crc_at - 8 - archive.index_offset);
    const uint32_t index_crc = crc(index);
    for (std::size_t i = 0; i < 4; ++i)
    {
        archive.bytes[crc_at + i] = static_cast<char>((index_crc >> (8 * i)) & 0xFFU);
    }

    EXPECT_EQ(read_error(archive.bytes), Error::corrupt_index);
}
