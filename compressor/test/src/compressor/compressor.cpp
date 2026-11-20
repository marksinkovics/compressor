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
