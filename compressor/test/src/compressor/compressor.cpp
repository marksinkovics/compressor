#include <algorithm>
#include <expected>
#include <filesystem>
#include <fstream>
#include <istream>
#include <iterator>
#include <memory>
#include <ostream>
#include <sstream>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <compressor/archive/metadata.h>
#include <compressor/compressor.h>
#include <compressor/error.h>
#include <compressor/stream/decoder.h>
#include <compressor/stream/encoder.h>
#include <compressor/test/mock/stream/decoder.h>
#include <compressor/test/mock/stream/encoder.h>

namespace
{

namespace fs = std::filesystem;
using ::testing::_;
using ::testing::HasSubstr;
using ::testing::Return;

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

// Copies the input to the output in upper case, standing in for either coder.
std::expected<void, compressor::Error> shout(std::istream& in, std::ostream& out)
{
    std::string text{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    std::ranges::transform(text, text.begin(), [](char c) {
        return static_cast<char>(c >= 'a' && c <= 'z' ? c - 'a' + 'A' : c);
    });
    out << text;
    return {};
}

// Writes something first, so a failure leaves a partial output behind.
std::expected<void, compressor::Error> fail_halfway(std::istream& /*in*/, std::ostream& out)
{
    out << "partial";
    return std::unexpected(compressor::Error::checksum_mismatch);
}

} // namespace

class CompressorTests : public ::testing::Test
{
protected:
    void SetUp() override
    {
        const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
        dir_ = fs::temp_directory_path() / (std::string("compressor_compressor_") + info->name());
        fs::remove_all(dir_);
        fs::create_directories(dir_);
        input_ = dir_ / "input.txt";
        output_ = dir_ / "output.bin";
        write_file(input_, "abc");
    }

    void TearDown() override
    {
        std::error_code error;
        fs::remove_all(dir_, error);
    }

    compressor::Compressor make_compressor()
    {
        return {std::move(encoder_), std::move(decoder_), {.in = in_, .out = out_, .err = err_}};
    }

    std::vector<std::string> files() const
    {
        std::vector<std::string> names;
        for (const auto& entry : fs::directory_iterator(dir_))
        {
            names.push_back(entry.path().filename().string());
        }
        std::ranges::sort(names);
        return names;
    }

    fs::path dir_;
    fs::path input_;
    fs::path output_;
    std::unique_ptr<MockIBlockEncoder> encoder_ = std::make_unique<MockIBlockEncoder>();
    std::unique_ptr<MockIBlockDecoder> decoder_ = std::make_unique<MockIBlockDecoder>();
    std::istringstream in_{"xyz"};
    std::ostringstream out_;
    std::ostringstream err_;
};

TEST_F(CompressorTests, Encode_streams_the_input_file_into_the_output_file)
{
    EXPECT_CALL(*encoder_, encode(_, _)).WillOnce(shout);
    EXPECT_CALL(*decoder_, decode(_, _)).Times(0);

    EXPECT_TRUE(make_compressor().encode(input_, output_));
    EXPECT_EQ(read_file(output_), "ABC");
    EXPECT_EQ(files(), (std::vector<std::string>{"input.txt", "output.bin"}));
}

TEST_F(CompressorTests, Decode_streams_the_input_file_into_the_output_file)
{
    EXPECT_CALL(*encoder_, encode(_, _)).Times(0);
    EXPECT_CALL(*decoder_, decode(_, _)).WillOnce(shout);

    EXPECT_TRUE(make_compressor().decode(input_, output_));
    EXPECT_EQ(read_file(output_), "ABC");
    EXPECT_EQ(files(), (std::vector<std::string>{"input.txt", "output.bin"}));
}

TEST_F(CompressorTests, An_existing_output_is_replaced)
{
    write_file(output_, std::string(4096, 'x'));
    EXPECT_CALL(*decoder_, decode(_, _)).WillOnce(shout);

    EXPECT_TRUE(make_compressor().decode(input_, output_));
    EXPECT_EQ(read_file(output_), "ABC");
}

TEST_F(CompressorTests, Encoding_a_file_onto_itself_works)
{
    EXPECT_CALL(*encoder_, encode(_, _)).WillOnce(shout);

    EXPECT_TRUE(make_compressor().encode(input_, input_));
    EXPECT_EQ(read_file(input_), "ABC");
    EXPECT_EQ(files(), (std::vector<std::string>{"input.txt"}));
}

TEST_F(CompressorTests, A_missing_input_fails_without_running_the_coder)
{
    write_file(output_, "keep");
    EXPECT_CALL(*encoder_, encode(_, _)).Times(0);
    EXPECT_CALL(*decoder_, decode(_, _)).Times(0);
    compressor::Compressor compressor = make_compressor();

    EXPECT_FALSE(compressor.encode(dir_ / "missing", output_));
    EXPECT_FALSE(compressor.decode(dir_ / "missing", output_));
    EXPECT_EQ(read_file(output_), "keep");
    EXPECT_THAT(err_.str(), HasSubstr("Cannot open input file"));
}

TEST_F(CompressorTests, An_output_in_a_missing_directory_fails_without_running_the_coder)
{
    EXPECT_CALL(*encoder_, encode(_, _)).Times(0);

    EXPECT_FALSE(make_compressor().encode(input_, dir_ / "missing" / "output.bin"));
    EXPECT_THAT(err_.str(), HasSubstr("Cannot open output file"));
}

TEST_F(CompressorTests, A_failed_decode_leaves_the_output_untouched_and_no_temporary_file)
{
    write_file(output_, "keep");
    EXPECT_CALL(*decoder_, decode(_, _)).WillOnce(fail_halfway);

    EXPECT_FALSE(make_compressor().decode(input_, output_));
    EXPECT_EQ(read_file(output_), "keep");
    EXPECT_EQ(files(), (std::vector<std::string>{"input.txt", "output.bin"}));
    EXPECT_THAT(err_.str(), HasSubstr("Cannot decode"));
    EXPECT_THAT(err_.str(), HasSubstr("checksum"));
}

TEST_F(CompressorTests, A_failed_decode_creates_no_output)
{
    EXPECT_CALL(*decoder_, decode(_, _)).WillOnce(fail_halfway);

    EXPECT_FALSE(make_compressor().decode(input_, output_));
    EXPECT_EQ(files(), (std::vector<std::string>{"input.txt"}));
}

TEST_F(CompressorTests, A_failed_encode_leaves_the_output_untouched)
{
    write_file(output_, "keep");
    EXPECT_CALL(*encoder_, encode(_, _))
        .WillOnce(Return(std::unexpected(compressor::Error::read_failed)));

    EXPECT_FALSE(make_compressor().encode(input_, output_));
    EXPECT_EQ(read_file(output_), "keep");
    EXPECT_EQ(files(), (std::vector<std::string>{"input.txt", "output.bin"}));
    EXPECT_THAT(err_.str(), HasSubstr("Cannot encode"));
}

#ifndef _WIN32
TEST_F(CompressorTests, A_device_output_is_written_in_place)
{
    EXPECT_CALL(*encoder_, encode(_, _)).WillOnce(shout);

    EXPECT_TRUE(make_compressor().encode(input_, "/dev/null"));
    EXPECT_EQ(err_.str(), "");
}
#endif

TEST_F(CompressorTests, Encode_reads_standard_input_and_writes_standard_output)
{
    EXPECT_CALL(*encoder_, encode(_, _)).WillOnce(shout);

    EXPECT_TRUE(make_compressor().encode("-", "-"));
    EXPECT_EQ(out_.str(), "XYZ");
    EXPECT_EQ(files(), (std::vector<std::string>{"input.txt"}));
    EXPECT_EQ(err_.str(), "");
}

TEST_F(CompressorTests, Standard_input_can_be_decoded_into_a_file)
{
    write_file(output_, std::string(4096, 'x'));
    EXPECT_CALL(*decoder_, decode(_, _)).WillOnce(shout);

    EXPECT_TRUE(make_compressor().decode("-", output_));
    EXPECT_EQ(read_file(output_), "XYZ");
    EXPECT_EQ(files(), (std::vector<std::string>{"input.txt", "output.bin"}));
}

TEST_F(CompressorTests, A_file_can_be_decoded_to_standard_output)
{
    EXPECT_CALL(*decoder_, decode(_, _)).WillOnce(shout);

    EXPECT_TRUE(make_compressor().decode(input_, "-"));
    EXPECT_EQ(out_.str(), "ABC");
    EXPECT_EQ(files(), (std::vector<std::string>{"input.txt"}));
}

TEST_F(CompressorTests, A_failed_decode_to_standard_output_keeps_what_was_written)
{
    EXPECT_CALL(*decoder_, decode(_, _)).WillOnce(fail_halfway);

    EXPECT_FALSE(make_compressor().decode("-", "-"));
    EXPECT_EQ(out_.str(), "partial");
    EXPECT_THAT(err_.str(), HasSubstr("Cannot decode standard input"));
}

TEST_F(CompressorTests, A_failing_standard_output_is_a_write_error)
{
    out_.setstate(std::ios::badbit);
    EXPECT_CALL(*encoder_, encode(_, _)).WillOnce(shout);

    EXPECT_FALSE(make_compressor().encode(input_, "-"));
    EXPECT_THAT(err_.str(), HasSubstr("could not be written"));
}

class CompressorFolderTests : public ::testing::Test
{
protected:
    void SetUp() override
    {
        const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
        dir_ = fs::temp_directory_path() / (std::string("compressor_folder_") + info->name());
        fs::remove_all(dir_);
        folder_ = dir_ / "folder";
        archive_ = dir_ / "folder.ingot";
        extracted_ = dir_ / "extracted";
        fs::create_directories(folder_ / "sub");
        write_file(folder_ / "a.txt", "alpha");
        write_file(folder_ / "sub" / "b.txt", std::string(10'000, 'b'));
    }

    void TearDown() override
    {
        std::error_code error;
        fs::remove_all(dir_, error);
    }

    compressor::Compressor make_compressor()
    {
        return {std::make_unique<compressor::BlockEncoder>(),
                std::make_unique<compressor::BlockDecoder>(),
                {.in = in_, .out = out_, .err = err_}};
    }

    std::vector<std::string> names(const fs::path& directory) const
    {
        std::vector<std::string> found;
        for (const auto& entry : fs::directory_iterator(directory))
        {
            found.push_back(entry.path().filename().string());
        }
        std::ranges::sort(found);
        return found;
    }

    fs::path dir_;
    fs::path folder_;
    fs::path archive_;
    fs::path extracted_;
    std::istringstream in_;
    std::ostringstream out_;
    std::ostringstream err_;
};

TEST_F(CompressorFolderTests, A_folder_round_trips_through_encode_and_decode)
{
    ASSERT_TRUE(make_compressor().encode(folder_, archive_)) << err_.str();
    ASSERT_TRUE(make_compressor().decode(archive_, extracted_)) << err_.str();

    EXPECT_EQ(read_file(extracted_ / "a.txt"), "alpha");
    EXPECT_EQ(read_file(extracted_ / "sub" / "b.txt"), std::string(10'000, 'b'));
    EXPECT_EQ(names(dir_), (std::vector<std::string>{"extracted", "folder", "folder.ingot"}));
    EXPECT_EQ(err_.str(), "");
}

TEST_F(CompressorFolderTests, An_archive_written_inside_the_folder_leaves_itself_out)
{
    const fs::path inside = folder_ / "self.ingot";
    write_file(inside, "previous archive");

    ASSERT_TRUE(make_compressor().encode(folder_, inside)) << err_.str();
    ASSERT_TRUE(make_compressor().decode(inside, extracted_)) << err_.str();
    EXPECT_EQ(names(extracted_), (std::vector<std::string>{"a.txt", "sub"}));
}

TEST_F(CompressorFolderTests, A_folder_can_be_encoded_to_standard_output)
{
    ASSERT_TRUE(make_compressor().encode(folder_, "-")) << err_.str();
    write_file(archive_, out_.str());

    ASSERT_TRUE(make_compressor().decode(archive_, extracted_)) << err_.str();
    EXPECT_EQ(read_file(extracted_ / "a.txt"), "alpha");
}

TEST_F(CompressorFolderTests, An_archive_is_not_decoded_into_an_existing_path)
{
    ASSERT_TRUE(make_compressor().encode(folder_, archive_));
    fs::create_directory(extracted_);

    EXPECT_FALSE(make_compressor().decode(archive_, extracted_));
    EXPECT_THAT(err_.str(), HasSubstr("already exists"));
    EXPECT_TRUE(fs::is_empty(extracted_));
}

TEST_F(CompressorFolderTests, An_archive_is_not_decoded_to_standard_output)
{
    ASSERT_TRUE(make_compressor().encode(folder_, archive_));

    EXPECT_FALSE(make_compressor().decode(archive_, "-"));
    EXPECT_THAT(err_.str(), HasSubstr("-o"));
    EXPECT_EQ(out_.str(), "");
}

TEST_F(CompressorFolderTests, An_archive_on_standard_input_is_rejected)
{
    ASSERT_TRUE(make_compressor().encode(folder_, archive_));
    in_.str(read_file(archive_));

    EXPECT_FALSE(make_compressor().decode("-", dir_ / "out"));
    EXPECT_THAT(err_.str(), HasSubstr("folder archive"));
    EXPECT_FALSE(fs::exists(dir_ / "out"));
}

TEST_F(CompressorFolderTests, A_failed_folder_decode_leaves_no_output_or_temporary_directory)
{
    ASSERT_TRUE(make_compressor().encode(folder_, archive_));
    std::string bytes = read_file(archive_);
    bytes.back() = 'Z';
    write_file(archive_, bytes);

    EXPECT_FALSE(make_compressor().decode(archive_, extracted_));
    EXPECT_THAT(err_.str(), HasSubstr("index is corrupt"));
    EXPECT_EQ(names(dir_), (std::vector<std::string>{"folder", "folder.ingot"}));
}

class CompressorArchiveCommandTests : public CompressorFolderTests
{
protected:
    void SetUp() override
    {
        CompressorFolderTests::SetUp();
        fs::create_symlink("a.txt", folder_ / "link");
        ASSERT_FALSE(compressor::set_mode(folder_ / "a.txt", 0640));
        ASSERT_FALSE(compressor::set_mtime(folder_ / "a.txt", 1'000'000'000'123'456'789));
        ASSERT_TRUE(make_compressor().encode(folder_, archive_)) << err_.str();
    }
};

TEST_F(CompressorArchiveCommandTests, List_prints_the_paths_in_the_archive)
{
    ASSERT_TRUE(make_compressor().list(archive_, false)) << err_.str();
    EXPECT_EQ(out_.str(), "a.txt\nlink\nsub/\nsub/b.txt\n");

    out_.str("");
    ASSERT_TRUE(make_compressor().list(archive_, true)) << err_.str();
    EXPECT_THAT(out_.str(), HasSubstr("-rw-r-----            5 2001-09-09 01:46:40 a.txt\n"));
}

TEST_F(CompressorArchiveCommandTests, List_rejects_a_file_that_is_not_an_archive)
{
    const fs::path plain = dir_ / "a.ingot";
    ASSERT_TRUE(make_compressor().encode(folder_ / "a.txt", plain));

    EXPECT_FALSE(make_compressor().list(plain, false));
    EXPECT_THAT(err_.str(), HasSubstr("not a folder archive"));
    EXPECT_FALSE(make_compressor().list("-", false));
    EXPECT_THAT(err_.str(), HasSubstr("must be read from a file"));
}

TEST_F(CompressorArchiveCommandTests, Extract_writes_a_file_with_its_mode_and_time)
{
    const fs::path output = dir_ / "a";
    write_file(output, "replaced");

    ASSERT_TRUE(make_compressor().extract(archive_, "./a.txt", output)) << err_.str();
    EXPECT_EQ(read_file(output), "alpha");
    const auto metadata = compressor::read_metadata(output);
    ASSERT_TRUE(metadata);
    EXPECT_EQ(metadata->mode, 0640);
    EXPECT_EQ(metadata->mtime_ns, 1'000'000'000'123'456'789);
}

TEST_F(CompressorArchiveCommandTests, Extract_writes_a_directory_into_a_new_directory)
{
    ASSERT_TRUE(make_compressor().extract(archive_, "sub/", extracted_)) << err_.str();
    EXPECT_EQ(names(extracted_), std::vector<std::string>{"b.txt"});
    EXPECT_EQ(read_file(extracted_ / "b.txt"), std::string(10'000, 'b'));

    EXPECT_FALSE(make_compressor().extract(archive_, "sub", extracted_));
    EXPECT_THAT(err_.str(), HasSubstr("already exists"));
    EXPECT_EQ(names(dir_), (std::vector<std::string>{"extracted", "folder", "folder.ingot"}));
}

TEST_F(CompressorArchiveCommandTests, Extract_writes_a_symlink)
{
    ASSERT_TRUE(make_compressor().extract(archive_, "link", dir_ / "l")) << err_.str();
    EXPECT_EQ(fs::read_symlink(dir_ / "l"), "a.txt");
}

TEST_F(CompressorArchiveCommandTests, A_file_can_be_extracted_to_standard_output)
{
    ASSERT_TRUE(make_compressor().extract(archive_, "sub/b.txt", "-")) << err_.str();
    EXPECT_EQ(out_.str(), std::string(10'000, 'b'));
}

TEST_F(CompressorArchiveCommandTests, Only_a_file_can_be_extracted_to_standard_output)
{
    EXPECT_FALSE(make_compressor().extract(archive_, "sub", "-"));
    EXPECT_THAT(err_.str(), HasSubstr("-o"));
    EXPECT_EQ(out_.str(), "");
}

TEST_F(CompressorArchiveCommandTests, A_path_not_in_the_archive_is_reported)
{
    EXPECT_FALSE(make_compressor().extract(archive_, "missing", dir_ / "m"));
    EXPECT_THAT(err_.str(), HasSubstr("missing: it is not in"));
    EXPECT_FALSE(fs::exists(dir_ / "m"));
}

TEST_F(CompressorArchiveCommandTests, Verify_checks_archives_and_plain_files)
{
    const fs::path plain = dir_ / "a.ingot";
    ASSERT_TRUE(make_compressor().encode(folder_ / "a.txt", plain));
    in_.str(read_file(plain));

    EXPECT_TRUE(make_compressor().verify(archive_)) << err_.str();
    EXPECT_TRUE(make_compressor().verify(plain)) << err_.str();
    EXPECT_TRUE(make_compressor().verify("-")) << err_.str();
    EXPECT_EQ(out_.str(),
              archive_.string() + ": OK\n" + plain.string() + ": OK\nstandard input: OK\n");
}

TEST_F(CompressorArchiveCommandTests, Verify_reports_a_damaged_archive)
{
    std::string bytes = read_file(archive_);
    bytes.back() = 'Z';
    write_file(archive_, bytes);

    EXPECT_FALSE(make_compressor().verify(archive_));
    EXPECT_THAT(err_.str(), HasSubstr("Cannot verify"));
    EXPECT_EQ(out_.str(), "");
}

TEST_F(CompressorArchiveCommandTests, Stats_prints_the_report)
{
    ASSERT_TRUE(make_compressor().stats(archive_, {.report = {.blocks = true}, .graph = {}}))
        << err_.str();
    EXPECT_THAT(out_.str(), HasSubstr("ingot folder archive with 2 files, 1 directory and 1 "
                                      "symlink\n"));
    EXPECT_THAT(out_.str(), HasSubstr("Content:  10005 bytes"));
    EXPECT_THAT(out_.str(), HasSubstr("Block  Method"));
}

TEST_F(CompressorArchiveCommandTests, Stats_writes_the_graph_to_a_file_or_instead_of_the_report)
{
    const fs::path graph = dir_ / "tree.dot";
    ASSERT_TRUE(make_compressor().stats(folder_ / "a.txt", {.report = {}, .graph = graph}))
        << err_.str();
    EXPECT_THAT(out_.str(), HasSubstr("not an ingot file"));
    EXPECT_THAT(read_file(graph), HasSubstr("digraph G {"));

    out_.str("");
    ASSERT_TRUE(make_compressor().stats(folder_ / "a.txt", {.report = {}, .graph = "-"}));
    EXPECT_EQ(out_.str(), read_file(graph));
}

TEST_F(CompressorArchiveCommandTests, Stats_reports_a_damaged_file_and_needs_a_file)
{
    std::string bytes = read_file(archive_);
    bytes.back() = 'Z';
    write_file(archive_, bytes);

    EXPECT_FALSE(make_compressor().stats(archive_, {}));
    EXPECT_THAT(err_.str(), HasSubstr("Cannot analyse"));
    EXPECT_FALSE(make_compressor().stats("-", {}));
    EXPECT_THAT(err_.str(), HasSubstr("needs a file"));
    EXPECT_EQ(out_.str(), "");
}

TEST_F(CompressorArchiveCommandTests, Grep_searches_the_files_of_an_archive)
{
    using compressor::SearchResult;
    const compressor::SearchOptions alpha{.pattern = "lph", .output = {}, .byte_offsets = false};
    EXPECT_EQ(make_compressor().grep(archive_, alpha, {}), SearchResult::matched) << err_.str();
    EXPECT_EQ(out_.str(), "a.txt:1:alpha\n");

    out_.str("");
    const compressor::SearchOptions bees{
        .pattern = "bbb", .output = compressor::SearchOutput::count, .byte_offsets = false};
    EXPECT_EQ(make_compressor().grep(archive_, bees, {"sub"}), SearchResult::matched);
    EXPECT_EQ(out_.str(), "sub/b.txt:1\n");

    EXPECT_EQ(make_compressor().grep(archive_, bees, {"a.txt"}), SearchResult::not_matched);
    EXPECT_EQ(make_compressor().grep(archive_, bees, {"missing"}), SearchResult::failed);
    EXPECT_THAT(err_.str(), HasSubstr("missing: it is not in"));
}

TEST_F(CompressorArchiveCommandTests, Grep_searches_a_plain_file_and_standard_input)
{
    using compressor::SearchResult;
    const fs::path plain = dir_ / "a.ingot";
    ASSERT_TRUE(make_compressor().encode(folder_ / "a.txt", plain));
    const compressor::SearchOptions names{
        .pattern = "alp", .output = compressor::SearchOutput::names, .byte_offsets = false};

    EXPECT_EQ(make_compressor().grep(plain, names, {}), SearchResult::matched) << err_.str();
    in_.str(read_file(plain));
    EXPECT_EQ(make_compressor().grep("-", names, {}), SearchResult::matched) << err_.str();
    EXPECT_EQ(out_.str(), plain.string() + "\n(standard input)\n");

    EXPECT_EQ(make_compressor().grep(plain, names, {"a.txt"}), SearchResult::failed);
    EXPECT_THAT(err_.str(), HasSubstr("not a folder archive"));
}

TEST_F(CompressorArchiveCommandTests, Grep_rejects_a_line_break_and_reports_damage)
{
    using compressor::SearchResult;
    const compressor::SearchOptions broken{.pattern = "a\nb", .output = {}, .byte_offsets = false};
    EXPECT_EQ(make_compressor().grep(archive_, broken, {}), SearchResult::failed);
    EXPECT_THAT(err_.str(), HasSubstr("line break"));

    std::string bytes = read_file(archive_);
    bytes.back() = 'Z';
    write_file(archive_, bytes);
    const compressor::SearchOptions any{.pattern = "a", .output = {}, .byte_offsets = false};
    EXPECT_EQ(make_compressor().grep(archive_, any, {}), SearchResult::failed);
    EXPECT_THAT(err_.str(), HasSubstr("Cannot search"));
}

TEST_F(CompressorArchiveCommandTests, A_file_encodes_as_gzip_and_a_folder_as_zip)
{
    using compressor::OutputFormat;
    const fs::path gz = dir_ / "a.txt.gz";
    ASSERT_TRUE(make_compressor().encode_as(folder_ / "a.txt", gz, OutputFormat::gzip))
        << err_.str();
    const std::string gzip = read_file(gz);
    ASSERT_GE(gzip.size(), 18U);
    EXPECT_EQ(gzip.substr(0, 3), "\x1f\x8b\x08");
    EXPECT_NE(gzip.find("a.txt"), std::string::npos);

    in_.str("from stdin");
    ASSERT_TRUE(make_compressor().encode_as("-", "-", OutputFormat::gzip)) << err_.str();
    EXPECT_EQ(out_.str().substr(0, 2), "\x1f\x8b");

    const fs::path zip = dir_ / "folder.zip";
    ASSERT_TRUE(make_compressor().encode_as(folder_, zip, OutputFormat::zip)) << err_.str();
    EXPECT_EQ(read_file(zip).substr(0, 4), "PK\x03\x04");
}

TEST_F(CompressorArchiveCommandTests, Gzip_takes_the_level_and_any_thread_count)
{
    using compressor::OutputFormat;
    std::string input;
    for (int i = 0; i < 120'000; ++i)
    {
        input += std::to_string(i % 1000) + (i % 7 == 0 ? "\n" : " ");
    }
    write_file(folder_ / "big.txt", input);
    const auto gzip = [&](unsigned threads, unsigned level) {
        const fs::path gz = dir_ / "big.gz";
        compressor::Compressor compressor({.threads = threads, .level = level});
        EXPECT_TRUE(compressor.encode_as(folder_ / "big.txt", gz, OutputFormat::gzip));
        return read_file(gz);
    };
    const std::string one_thread = gzip(1, 6);
    EXPECT_EQ(gzip(4, 6), one_thread);
    EXPECT_GT(gzip(4, 1).size(), gzip(4, 9).size());

    const fs::path decoded = dir_ / "big.out";
    write_file(dir_ / "big.gz", one_thread);
    ASSERT_TRUE(make_compressor().decode(dir_ / "big.gz", decoded)) << err_.str();
    EXPECT_EQ(read_file(decoded), input);
}

TEST_F(CompressorArchiveCommandTests, Gzip_needs_a_file_and_zip_a_folder)
{
    using compressor::OutputFormat;
    EXPECT_FALSE(make_compressor().encode_as(folder_, dir_ / "x.gz", OutputFormat::gzip));
    EXPECT_THAT(err_.str(), HasSubstr("use the zip format"));
    EXPECT_FALSE(make_compressor().encode_as(folder_ / "a.txt", dir_ / "x.zip", OutputFormat::zip));
    EXPECT_THAT(err_.str(), HasSubstr("use the gzip format"));
    EXPECT_FALSE(fs::exists(dir_ / "x.gz"));
    EXPECT_FALSE(fs::exists(dir_ / "x.zip"));
}

TEST_F(CompressorArchiveCommandTests, Search_finds_offsets_through_the_search_index)
{
    using compressor::SearchResult;
    const fs::path indexed = dir_ / "indexed.ingot";
    compressor::Compressor writer(std::make_unique<compressor::BlockEncoder>(),
                                  std::make_unique<compressor::BlockDecoder>(),
                                  {.in = in_, .out = out_, .err = err_}, true);
    ASSERT_TRUE(writer.encode(folder_, indexed)) << err_.str();

    EXPECT_EQ(make_compressor().search(indexed, "lph", false), SearchResult::matched) << err_.str();
    EXPECT_EQ(out_.str(), "a.txt:1\n");
    out_.str("");
    EXPECT_EQ(make_compressor().search(indexed, "bbbb", true), SearchResult::matched);
    EXPECT_EQ(out_.str(), "sub/b.txt:9997\n");
    out_.str("");
    EXPECT_EQ(make_compressor().search(indexed, "absent", false), SearchResult::not_matched);
    EXPECT_EQ(out_.str(), "");

    EXPECT_TRUE(make_compressor().verify(indexed)) << err_.str();
    ASSERT_TRUE(make_compressor().decode(indexed, extracted_)) << err_.str();
    EXPECT_EQ(read_file(extracted_ / "a.txt"), "alpha");
}

TEST_F(CompressorArchiveCommandTests, Search_needs_a_search_index)
{
    EXPECT_EQ(make_compressor().search(archive_, "a", false), compressor::SearchResult::failed);
    EXPECT_THAT(err_.str(), HasSubstr("no search index"));
}

TEST_F(CompressorArchiveCommandTests, A_damaged_search_index_fails_verify)
{
    const fs::path indexed = dir_ / "indexed.ingot";
    compressor::Compressor writer(std::make_unique<compressor::BlockEncoder>(),
                                  std::make_unique<compressor::BlockDecoder>(),
                                  {.in = in_, .out = out_, .err = err_}, true);
    ASSERT_TRUE(writer.encode(folder_, indexed));
    std::string bytes = read_file(indexed);
    bytes[bytes.size() - 30] =
        static_cast<char>(static_cast<uint8_t>(bytes[bytes.size() - 30]) ^ 1U);
    write_file(indexed, bytes);

    EXPECT_FALSE(make_compressor().verify(indexed));
    EXPECT_THAT(err_.str(), HasSubstr("search index"));
}

TEST_F(CompressorArchiveCommandTests, Gzip_files_decode_verify_and_search)
{
    using compressor::OutputFormat;
    const fs::path gz = dir_ / "b.txt.gz";
    ASSERT_TRUE(make_compressor().encode_as(folder_ / "sub" / "b.txt", gz, OutputFormat::gzip));
    const fs::path decoded = dir_ / "b.txt";
    ASSERT_TRUE(make_compressor().decode(gz, decoded)) << err_.str();
    EXPECT_EQ(read_file(decoded), std::string(10'000, 'b'));

    in_.str(read_file(gz));
    ASSERT_TRUE(make_compressor().decode("-", "-")) << err_.str();
    EXPECT_EQ(out_.str(), std::string(10'000, 'b'));

    out_.str("");
    EXPECT_TRUE(make_compressor().verify(gz)) << err_.str();
    const compressor::SearchOptions count{
        .pattern = "bb", .output = compressor::SearchOutput::count, .byte_offsets = false};
    EXPECT_EQ(make_compressor().grep(gz, count, {}), compressor::SearchResult::matched);
    EXPECT_EQ(out_.str(), gz.string() + ": OK\n1\n");
}

TEST_F(CompressorArchiveCommandTests, A_damaged_gzip_file_fails_and_leaves_no_output)
{
    const fs::path gz = dir_ / "a.gz";
    ASSERT_TRUE(make_compressor().encode_as(folder_ / "a.txt", gz, compressor::OutputFormat::gzip));
    std::string bytes = read_file(gz);
    bytes[bytes.size() - 6] = static_cast<char>(static_cast<uint8_t>(bytes[bytes.size() - 6]) ^ 1U);
    write_file(gz, bytes);

    EXPECT_FALSE(make_compressor().decode(gz, dir_ / "a.out"));
    EXPECT_THAT(err_.str(), HasSubstr("checksum"));
    EXPECT_FALSE(fs::exists(dir_ / "a.out"));
}

TEST_F(CompressorArchiveCommandTests, A_very_common_pattern_is_counted_by_scanning)
{
    // Over 100 000 matches: search decodes and scans instead of locating each one.
    const std::string many(150'000, 'z');
    write_file(folder_ / "many.txt", many);
    write_file(folder_ / "same.txt", many);
    write_file(folder_ / "edge.txt", "zz");
    const fs::path indexed = dir_ / "indexed.ingot";
    compressor::Compressor writer(std::make_unique<compressor::BlockEncoder>(),
                                  std::make_unique<compressor::BlockDecoder>(),
                                  {.in = in_, .out = out_, .err = err_}, true);
    ASSERT_TRUE(writer.encode(folder_, indexed)) << err_.str();

    ASSERT_EQ(make_compressor().search(indexed, "zz", true), compressor::SearchResult::matched)
        << err_.str();
    // Matches never span two files, though their data runs together.
    EXPECT_EQ(out_.str(), "edge.txt:1\nmany.txt:149999\nsame.txt:149999\n");
}
