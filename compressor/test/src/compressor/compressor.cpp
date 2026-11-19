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
