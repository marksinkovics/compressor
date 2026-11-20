#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <ranges>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <sys/stat.h>

#include <compressor/archive/archive.h>
#include <compressor/archive/index.h>
#include <compressor/archive/metadata.h>
#include <compressor/data/data.h>
#include <compressor/engine/engine.h>
#include <compressor/error.h>
#include <compressor/stream/decoder.h>
#include <compressor/stream/encoder.h>

namespace
{

namespace fs = std::filesystem;
using compressor::Entry;
using compressor::EntryType;
using compressor::Error;
using ::testing::ElementsAre;
using ::testing::HasSubstr;
using ::testing::IsEmpty;

constexpr uint8_t block_size_log2 = compressor::min_block_size_log2;
constexpr std::size_t block_size = std::size_t{1} << block_size_log2;
constexpr unsigned threads = 3;

std::string read_file(const fs::path& path)
{
    std::ifstream stream(path, std::ios::in | std::ios::binary);
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

void write_file(const fs::path& path, const std::string& bytes)
{
    std::ofstream stream(path, std::ios::out | std::ios::trunc | std::ios::binary);
    stream << bytes;
}

std::string pattern(std::size_t size, char first)
{
    std::string text(size, '\0');
    for (std::size_t i = 0; i < size; ++i)
    {
        text[i] = static_cast<char>(first + static_cast<char>((i * i) % 7));
    }
    return text;
}

compressor::EngineFactory engines()
{
    return [] {
        return std::make_unique<compressor::Engine>(block_size_log2);
    };
}

// Random-looking bytes stay stored, so a changed byte decodes and only the CRC notices.
std::string noise(std::size_t size)
{
    std::string bytes(size, '\0');
    uint32_t state = 1;
    for (char& c : bytes)
    {
        state = (state * 1103515245U) + 12345U;
        c = static_cast<char>(state >> 16U);
    }
    return bytes;
}

void flip_payload_byte(std::string& bytes, uint64_t block_offset)
{
    char& byte = bytes[block_offset + compressor::block_header_bytes + 5];
    byte = static_cast<char>(static_cast<uint8_t>(byte) ^ 1U);
}

std::vector<std::string> paths(const std::vector<Entry>& entries)
{
    std::vector<std::string> names;
    names.reserve(entries.size());
    for (const Entry& entry : entries)
    {
        names.push_back(entry.path);
    }
    return names;
}

} // namespace

class ArchiveTests : public ::testing::Test
{
protected:
    void SetUp() override
    {
        const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
        dir_ = fs::temp_directory_path() / (std::string("compressor_archive_") + info->name());
        fs::remove_all(dir_);
        root_ = dir_ / "root";
        out_ = dir_ / "out";
        fs::create_directories(root_);
        fs::create_directories(out_);
    }

    void TearDown() override
    {
        std::error_code error;
        fs::remove_all(dir_, error);
    }

    // The scan of `root`, which must succeed.
    std::vector<Entry> scan(const fs::path& root, const fs::path& exclude = {})
    {
        auto entries = compressor::scan_tree(root, exclude, warnings_);
        EXPECT_TRUE(entries) << entries.error().detail;
        return entries ? *entries : std::vector<Entry>{};
    }

    std::expected<std::string, compressor::Failure> archive(const fs::path& root)
    {
        auto entries = compressor::scan_tree(root, {}, warnings_);
        if (!entries)
        {
            return std::unexpected(entries.error());
        }
        compressor::BlockEncoder encoder(engines(), block_size_log2, threads);
        std::ostringstream out;
        if (auto written = compressor::write_archive(encoder, root, *entries, out); !written)
        {
            return std::unexpected(written.error());
        }
        return out.str();
    }

    static compressor::ArchiveInfo info_of(const std::string& bytes)
    {
        std::istringstream in(bytes);
        auto info = compressor::read_archive_index(in);
        EXPECT_TRUE(info);
        return info ? *info : compressor::ArchiveInfo{};
    }

    static std::expected<std::string, compressor::Failure> extract_file(const std::string& bytes,
                                                                        const std::string& path)
    {
        const compressor::ArchiveInfo info = info_of(bytes);
        const auto file = std::ranges::find(info.index.entries, path, &Entry::path);
        EXPECT_NE(file, info.index.entries.end()) << path;
        compressor::BlockDecoder decoder(engines(), threads);
        std::istringstream in(bytes);
        std::ostringstream out;
        if (auto written = compressor::extract_file(decoder, in, info, *file, out); !written)
        {
            return std::unexpected(written.error());
        }
        return out.str();
    }

    static std::expected<void, compressor::Failure> verify(const std::string& bytes)
    {
        compressor::BlockDecoder decoder(engines(), threads);
        std::istringstream in(bytes);
        return compressor::verify_archive(decoder, in, info_of(bytes));
    }

    std::expected<void, compressor::Failure> extract(const std::string& bytes)
    {
        compressor::BlockDecoder decoder(engines(), threads);
        std::istringstream in(bytes);
        return compressor::extract_archive(decoder, in, out_);
    }

    // A tree with nested and empty directories, empty and multi-block files, and symlinks.
    void make_sample_tree()
    {
        fs::create_directories(root_ / "docs" / "deep" / "er");
        fs::create_directories(root_ / "empty");
        write_file(root_ / "a.txt", "hello archive\n");
        write_file(root_ / "big.bin", pattern((3 * block_size) + 17, 'a'));
        write_file(root_ / "docs" / "zero", "");
        write_file(root_ / "docs" / "deep" / "er" / "note.md", pattern(100, 'm'));
        write_file(root_ / "run.sh", "#!/bin/sh\n");
        fs::create_symlink("a.txt", root_ / "link");
        fs::create_symlink("../missing", root_ / "docs" / "dangling");
        ASSERT_FALSE(compressor::set_mode(root_ / "run.sh", 0755));
        ASSERT_FALSE(compressor::set_mode(root_ / "a.txt", 0600));
        ASSERT_FALSE(compressor::set_mode(root_ / "docs" / "deep", 0750));
        int64_t time = -86'400'123'456'789; // before 1970, with nanoseconds
        for (const Entry& entry : scan(root_) | std::views::reverse)
        {
            ASSERT_FALSE(compressor::set_mtime(root_ / entry.path, time));
            time += 3'600'000'000'001;
        }
    }

    fs::path dir_;
    fs::path root_;
    fs::path out_;
    std::ostringstream warnings_;
};

TEST_F(ArchiveTests, The_scan_lists_each_directory_before_its_contents_in_name_order)
{
    make_sample_tree();

    EXPECT_THAT(paths(scan(root_)), ElementsAre("a.txt", "big.bin", "docs", "docs/dangling",
                                                "docs/deep", "docs/deep/er", "docs/deep/er/note.md",
                                                "docs/zero", "empty", "link", "run.sh"));
    EXPECT_THAT(warnings_.str(), IsEmpty());
}

TEST_F(ArchiveTests, The_scan_records_types_modes_times_and_targets)
{
    make_sample_tree();
    const std::vector<Entry> entries = scan(root_);

    const auto find = [&](const std::string& path) {
        return *std::ranges::find(entries, path, &Entry::path);
    };
    EXPECT_EQ(find("run.sh").type, EntryType::file);
    EXPECT_EQ(find("run.sh").mode, 0755);
    EXPECT_EQ(find("a.txt").mode, 0600);
    EXPECT_EQ(find("a.txt").size, 14U);
    EXPECT_EQ(find("docs/deep").type, EntryType::directory);
    EXPECT_EQ(find("docs/deep").mode, 0750);
    EXPECT_EQ(find("link").type, EntryType::symlink);
    EXPECT_EQ(find("link").target, "a.txt");
    EXPECT_EQ(find("docs/dangling").target, "../missing");
    EXPECT_EQ(find("run.sh").mtime_ns, -86'400'123'456'789);
}

TEST_F(ArchiveTests, A_tree_round_trips_with_contents_modes_times_and_symlinks)
{
    make_sample_tree();
    const auto bytes = archive(root_);
    ASSERT_TRUE(bytes) << bytes.error().detail;

    const auto extracted = extract(*bytes);
    ASSERT_TRUE(extracted) << compressor::describe(extracted.error().error) << ' '
                           << extracted.error().detail;

    std::vector<Entry> expected = scan(root_);
    std::vector<Entry> actual = scan(out_);
    ASSERT_EQ(paths(actual), paths(expected));
    for (std::size_t i = 0; i < expected.size(); ++i)
    {
        const Entry& want = expected[i];
        const Entry& got = actual[i];
        EXPECT_EQ(got.type, want.type) << want.path;
        EXPECT_EQ(got.mtime_ns, want.mtime_ns) << want.path;
        EXPECT_EQ(got.target, want.target) << want.path;
        if (want.type != EntryType::symlink)
        {
            EXPECT_EQ(got.mode, want.mode) << want.path;
        }
        if (want.type == EntryType::file)
        {
            EXPECT_EQ(read_file(out_ / want.path), read_file(root_ / want.path)) << want.path;
        }
    }
}

TEST_F(ArchiveTests, The_archive_is_a_container_with_the_files_back_to_back)
{
    write_file(root_ / "a", "first ");
    write_file(root_ / "b", "second");
    const auto bytes = archive(root_);
    ASSERT_TRUE(bytes);

    compressor::BlockDecoder decoder(engines(), 1);
    std::istringstream in(*bytes);
    std::ostringstream data;
    ASSERT_TRUE(decoder.decode_frame(in, data, compressor::archive_flag));
    EXPECT_EQ(data.str(), "first second");

    std::istringstream index_in(*bytes);
    const auto info = compressor::read_archive_index(index_in);
    ASSERT_TRUE(info);
    EXPECT_EQ(info->index.entries[1].offset, 6U);
    EXPECT_EQ(info->index.entries[1].size, 6U);
}

TEST_F(ArchiveTests, An_empty_folder_round_trips)
{
    const auto bytes = archive(root_);
    ASSERT_TRUE(bytes);

    ASSERT_TRUE(extract(*bytes));
    EXPECT_TRUE(fs::is_empty(out_));
}

TEST_F(ArchiveTests, The_scan_skips_the_excluded_path_and_special_files)
{
    write_file(root_ / "keep", "k");
    write_file(root_ / "out.ingot", "old archive");
    ASSERT_EQ(::mkfifo((root_ / "pipe").c_str(), 0600), 0);

    EXPECT_THAT(paths(scan(root_, root_ / "out.ingot")), ElementsAre("keep"));
    EXPECT_THAT(warnings_.str(), HasSubstr("Skipping " + (root_ / "pipe").string()));
}

TEST_F(ArchiveTests, A_damaged_data_byte_fails_with_a_checksum_mismatch)
{
    write_file(root_ / "noise", noise(block_size));
    auto bytes = archive(root_);
    ASSERT_TRUE(bytes);
    flip_payload_byte(*bytes, compressor::frame_header_bytes);

    const auto extracted = extract(*bytes);
    ASSERT_FALSE(extracted);
    EXPECT_EQ(extracted.error().error, Error::checksum_mismatch);
}

TEST_F(ArchiveTests, A_damaged_index_fails_before_writing_anything)
{
    write_file(root_ / "a", "abc");
    auto bytes = archive(root_);
    ASSERT_TRUE(bytes);
    bytes->back() = 'Z';

    const auto extracted = extract(*bytes);
    ASSERT_FALSE(extracted);
    EXPECT_EQ(extracted.error().error, Error::corrupt_index);
    EXPECT_TRUE(fs::is_empty(out_));
}

TEST_F(ArchiveTests, An_unreadable_file_names_its_path)
{
    write_file(root_ / "secret", "s");
    ASSERT_FALSE(compressor::set_mode(root_ / "secret", 0));

    const auto bytes = archive(root_);
    ASSERT_FALSE(compressor::set_mode(root_ / "secret", 0600));
    ASSERT_FALSE(bytes);
    EXPECT_EQ(bytes.error().error, Error::file_failed);
    EXPECT_THAT(bytes.error().detail, HasSubstr((root_ / "secret").string()));
}

TEST_F(ArchiveTests, A_file_that_changes_size_is_archived_as_read)
{
    write_file(root_ / "a", "abc");
    auto entries = compressor::scan_tree(root_, {}, warnings_);
    ASSERT_TRUE(entries);
    write_file(root_ / "a", "abcdef");

    compressor::BlockEncoder encoder(engines(), block_size_log2, 1);
    std::ostringstream out;
    ASSERT_TRUE(compressor::write_archive(encoder, root_, *entries, out));
    ASSERT_TRUE(extract(out.str()));
    EXPECT_EQ(read_file(out_ / "a"), "abcdef");
}

TEST_F(ArchiveTests, Only_archives_are_recognised_as_archives)
{
    write_file(root_ / "a", "abc");
    const auto bytes = archive(root_);
    ASSERT_TRUE(bytes);
    std::istringstream archive_in(*bytes);
    EXPECT_TRUE(compressor::is_archive(archive_in));
    EXPECT_EQ(archive_in.tellg(), 0);

    compressor::BlockEncoder encoder(engines(), block_size_log2, 1);
    std::istringstream text("abc");
    std::ostringstream plain;
    ASSERT_TRUE(encoder.encode(text, plain));
    std::istringstream plain_in(plain.str());
    EXPECT_FALSE(compressor::is_archive(plain_in));
    std::istringstream other("not ingot");
    EXPECT_FALSE(compressor::is_archive(other));
}

TEST(ArchivePathTests, Paths_are_normalised_to_the_index_spelling)
{
    EXPECT_EQ(compressor::archive_path("a.txt"), "a.txt");
    EXPECT_EQ(compressor::archive_path("./docs//deep/"), "docs/deep");
    EXPECT_EQ(compressor::archive_path("docs/../a.txt"), "a.txt");
}

TEST(ArchiveSubtreeTests, A_subtree_is_the_entry_and_everything_under_it_relative_to_it)
{
    const std::vector<Entry> entries{
        {.type = EntryType::directory, .path = "docs"},
        {.type = EntryType::file, .path = "docs/a"},
        {.type = EntryType::directory, .path = "docs/deep"},
        {.type = EntryType::symlink, .path = "docs/deep/l"},
        {.type = EntryType::directory, .path = "docs2"},
        {.type = EntryType::file, .path = "docs2/b"},
    };

    const auto docs = compressor::select_subtree(entries, "docs");
    ASSERT_TRUE(docs);
    EXPECT_THAT(paths(*docs), ElementsAre("", "a", "deep", "deep/l"));
    EXPECT_EQ(docs->front().type, EntryType::directory);

    const auto file = compressor::select_subtree(entries, "docs/a");
    ASSERT_TRUE(file);
    EXPECT_THAT(paths(*file), ElementsAre(""));
    EXPECT_EQ(file->front().type, EntryType::file);

    EXPECT_FALSE(compressor::select_subtree(entries, "doc"));
    EXPECT_FALSE(compressor::select_subtree(entries, ""));
}

TEST(ArchiveListTests, The_listing_names_each_entry_and_details_add_mode_size_and_utc_time)
{
    compressor::ArchiveInfo info;
    info.index.entries = {
        {.type = EntryType::directory, .path = "d", .mode = 0750},
        {.type = EntryType::file,
         .path = "d/f",
         .mode = 04755,
         .mtime_ns = 1'700'000'000'123'456'789,
         .size = 3},
        {.type = EntryType::file, .path = "g", .mode = 02640},
        {.type = EntryType::symlink, .path = "l", .mode = 0777, .mtime_ns = -1, .target = "d/f"},
        {.type = EntryType::directory, .path = "t", .mode = 01777},
    };

    std::ostringstream names;
    compressor::list_archive(info, false, names);
    EXPECT_EQ(names.str(), "d/\nd/f\ng\nl\nt/\n");

    std::ostringstream details;
    compressor::list_archive(info, true, details);
    EXPECT_EQ(details.str(), "drwxr-x---            0 1970-01-01 00:00:00 d/\n"
                             "-rwsr-xr-x            3 2023-11-14 22:13:20 d/f\n"
                             "-rw-r-S---            0 1970-01-01 00:00:00 g\n"
                             "lrwxrwxrwx            3 1969-12-31 23:59:59 l -> d/f\n"
                             "drwxrwxrwt            0 1970-01-01 00:00:00 t/\n");
}

TEST_F(ArchiveTests, A_subtree_extracts_with_contents_modes_and_times_including_its_root)
{
    make_sample_tree();
    const auto bytes = archive(root_);
    ASSERT_TRUE(bytes);
    const compressor::ArchiveInfo info = info_of(*bytes);
    const auto docs = compressor::select_subtree(info.index.entries, "docs");
    ASSERT_TRUE(docs);

    compressor::BlockDecoder decoder(engines(), threads);
    std::istringstream in(*bytes);
    const auto extracted = compressor::extract_entries(decoder, in, info, *docs, out_);
    ASSERT_TRUE(extracted) << extracted.error().detail;

    const std::vector<Entry> expected = scan(root_ / "docs");
    const std::vector<Entry> actual = scan(out_);
    ASSERT_EQ(paths(actual), paths(expected));
    for (std::size_t i = 0; i < expected.size(); ++i)
    {
        EXPECT_EQ(actual[i].mtime_ns, expected[i].mtime_ns) << expected[i].path;
        EXPECT_EQ(actual[i].target, expected[i].target) << expected[i].path;
    }
    EXPECT_EQ(read_file(out_ / "deep" / "er" / "note.md"), pattern(100, 'm'));
    const auto root = compressor::read_metadata(out_);
    const auto original = compressor::read_metadata(root_ / "docs");
    ASSERT_TRUE(root && original);
    EXPECT_EQ(root->mode, original->mode);
    EXPECT_EQ(root->mtime_ns, original->mtime_ns);
}

TEST_F(ArchiveTests, A_file_is_written_whole_from_its_blocks)
{
    make_sample_tree();
    const auto bytes = archive(root_);
    ASSERT_TRUE(bytes);

    EXPECT_EQ(extract_file(*bytes, "big.bin"), read_file(root_ / "big.bin"));
    EXPECT_EQ(extract_file(*bytes, "a.txt"), "hello archive\n");
    EXPECT_EQ(extract_file(*bytes, "docs/zero"), "");
}

TEST_F(ArchiveTests, A_file_is_decoded_only_from_the_blocks_that_hold_it)
{
    write_file(root_ / "a.bin", noise(2 * block_size));
    write_file(root_ / "b.txt", "after the noise");
    auto bytes = archive(root_);
    ASSERT_TRUE(bytes);
    flip_payload_byte(*bytes, info_of(*bytes).index.block_offsets[0]);

    EXPECT_EQ(extract_file(*bytes, "b.txt"), "after the noise");
    const auto damaged = extract_file(*bytes, "a.bin");
    ASSERT_FALSE(damaged);
    EXPECT_EQ(damaged.error().error, Error::checksum_mismatch);
}

TEST_F(ArchiveTests, Verify_accepts_an_intact_archive)
{
    make_sample_tree();
    const auto bytes = archive(root_);
    ASSERT_TRUE(bytes);
    EXPECT_TRUE(verify(*bytes));
}

TEST_F(ArchiveTests, Verify_finds_a_damaged_last_block)
{
    write_file(root_ / "noise", noise((2 * block_size) + 100));
    auto bytes = archive(root_);
    ASSERT_TRUE(bytes);
    flip_payload_byte(*bytes, info_of(*bytes).index.block_offsets.back());
    const auto verified = verify(*bytes);
    ASSERT_FALSE(verified);
    EXPECT_EQ(verified.error().error, Error::checksum_mismatch);
}

TEST_F(ArchiveTests, Verify_accepts_an_empty_archive)
{
    const auto bytes = archive(root_);
    ASSERT_TRUE(bytes);
    EXPECT_TRUE(verify(*bytes));
}
