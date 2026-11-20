#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <compressor/test/mock/compressor.h>

#include <compressor/cli.h>

namespace fs = std::filesystem;
using ::testing::HasSubstr;
using ::testing::IsEmpty;
using ::testing::Not;
using ::testing::Return;

class CLITests : public ::testing::Test
{
protected:
    void SetUp() override
    {
        const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
        dir_ = fs::temp_directory_path() / (std::string("compressor_cli_") + info->name());
        fs::remove_all(dir_);
        fs::create_directories(dir_);
        input_ = (dir_ / "input.txt").string();
        output_ = (dir_ / "output.bin").string();
        std::ofstream(input_) << "abc";
    }

    void TearDown() override
    {
        fs::remove_all(dir_);
    }

    int run(std::initializer_list<std::string> args)
    {
        std::vector<std::string> storage{"compressor_app"};
        storage.insert(storage.end(), args);
        std::vector<const char*> argv;
        argv.reserve(storage.size());
        for (const auto& arg : storage)
            argv.push_back(arg.c_str());

        compressor::CLI cli([this](unsigned threads) {
            threads_ = threads;
            return std::move(compressor_);
        }, out_, err_);
        return cli.run(argv);
    }

    fs::path dir_;
    std::string input_;
    std::string output_;
    std::unique_ptr<MockICompressor> compressor_ = std::make_unique<MockICompressor>();
    std::optional<unsigned> threads_;
    std::ostringstream out_;
    std::ostringstream err_;
};

TEST_F(CLITests, Encode_passes_the_paths_to_the_compressor)
{
    std::string input;
    std::string output;
    EXPECT_CALL(*compressor_, encode(::testing::_, ::testing::_))
        .WillOnce([&](const fs::path& in, const fs::path& out) {
        input = in.string();
        output = out.string();
        return true;
    });
    EXPECT_CALL(*compressor_, decode(::testing::_, ::testing::_)).Times(0);

    EXPECT_EQ(run({"encode", "--input", input_, "--output", output_}), EXIT_SUCCESS);
    EXPECT_EQ(input, input_);
    EXPECT_EQ(output, output_);
}

TEST_F(CLITests, Decode_passes_the_paths_to_the_compressor)
{
    std::string input;
    std::string output;
    EXPECT_CALL(*compressor_, encode(::testing::_, ::testing::_)).Times(0);
    EXPECT_CALL(*compressor_, decode(::testing::_, ::testing::_))
        .WillOnce([&](const fs::path& in, const fs::path& out) {
        input = in.string();
        output = out.string();
        return true;
    });

    EXPECT_EQ(run({"decode", "-i", input_, "-o", output_}), EXIT_SUCCESS);
    EXPECT_EQ(input, input_);
    EXPECT_EQ(output, output_);
}

TEST_F(CLITests, Failed_encoding_gives_a_non_zero_exit_code)
{
    EXPECT_CALL(*compressor_, encode(::testing::_, ::testing::_)).WillOnce(Return(false));

    EXPECT_NE(run({"encode", "-i", input_, "-o", output_}), EXIT_SUCCESS);
    EXPECT_THAT(err_.str(), HasSubstr("Encoding"));
}

TEST_F(CLITests, Failed_decoding_gives_a_non_zero_exit_code)
{
    EXPECT_CALL(*compressor_, decode(::testing::_, ::testing::_)).WillOnce(Return(false));

    EXPECT_NE(run({"decode", "-i", input_, "-o", output_}), EXIT_SUCCESS);
    EXPECT_THAT(err_.str(), HasSubstr("Decoding"));
}

TEST_F(CLITests, Help_prints_once_and_succeeds)
{
    EXPECT_CALL(*compressor_, encode(::testing::_, ::testing::_)).Times(0);
    EXPECT_CALL(*compressor_, decode(::testing::_, ::testing::_)).Times(0);

    EXPECT_EQ(run({"--help"}), EXIT_SUCCESS);
    const std::string help = out_.str();
    const auto first = help.find("OPTIONS:");
    ASSERT_NE(first, std::string::npos);
    EXPECT_EQ(help.find("OPTIONS:", first + 1), std::string::npos);
    EXPECT_THAT(help, HasSubstr("encode"));
    EXPECT_THAT(help, HasSubstr("decode"));
    EXPECT_THAT(err_.str(), IsEmpty());
}

TEST_F(CLITests, Subcommand_help_succeeds)
{
    EXPECT_CALL(*compressor_, encode(::testing::_, ::testing::_)).Times(0);

    EXPECT_EQ(run({"encode", "--help"}), EXIT_SUCCESS);
    EXPECT_THAT(out_.str(), HasSubstr("--input"));
}

TEST_F(CLITests, Version_prints_the_version_and_succeeds)
{
    EXPECT_CALL(*compressor_, encode(::testing::_, ::testing::_)).Times(0);
    EXPECT_CALL(*compressor_, decode(::testing::_, ::testing::_)).Times(0);

    EXPECT_EQ(run({"--version"}), EXIT_SUCCESS);
    EXPECT_THAT(out_.str(), HasSubstr(COMPRESSOR_VERSION));
}

TEST_F(CLITests, No_subcommand_fails)
{
    EXPECT_CALL(*compressor_, encode(::testing::_, ::testing::_)).Times(0);
    EXPECT_CALL(*compressor_, decode(::testing::_, ::testing::_)).Times(0);

    EXPECT_NE(run({}), EXIT_SUCCESS);
    EXPECT_THAT(err_.str(), Not(IsEmpty()));
}

TEST_F(CLITests, Two_subcommands_fail)
{
    EXPECT_CALL(*compressor_, encode(::testing::_, ::testing::_)).Times(0);
    EXPECT_CALL(*compressor_, decode(::testing::_, ::testing::_)).Times(0);

    EXPECT_NE(run({"encode", "-i", input_, "-o", output_, "decode", "-i", input_, "-o", output_}),
              EXIT_SUCCESS);
}

TEST_F(CLITests, Missing_input_file_fails)
{
    EXPECT_CALL(*compressor_, encode(::testing::_, ::testing::_)).Times(0);

    EXPECT_NE(run({"encode", "-i", (dir_ / "missing").string(), "-o", output_}), EXIT_SUCCESS);
    EXPECT_THAT(err_.str(), HasSubstr("missing"));
}

TEST_F(CLITests, Option_without_a_value_at_the_end_fails)
{
    EXPECT_CALL(*compressor_, encode(::testing::_, ::testing::_)).Times(0);

    EXPECT_NE(run({"encode", "-o", output_, "--input"}), EXIT_SUCCESS);
}

TEST_F(CLITests, Omitted_paths_mean_the_standard_streams)
{
    EXPECT_CALL(*compressor_, decode(fs::path("-"), fs::path("-"))).WillOnce(Return(true));

    EXPECT_EQ(run({"decode"}), EXIT_SUCCESS);
}

TEST_F(CLITests, A_dash_input_means_standard_input)
{
    EXPECT_CALL(*compressor_, encode(fs::path("-"), fs::path(output_))).WillOnce(Return(true));

    EXPECT_EQ(run({"encode", "-i", "-", "-o", output_}), EXIT_SUCCESS);
}

TEST_F(CLITests, A_dash_output_means_standard_output)
{
    EXPECT_CALL(*compressor_, decode(fs::path(input_), fs::path("-"))).WillOnce(Return(true));

    EXPECT_EQ(run({"decode", "-i", input_, "-o", "-"}), EXIT_SUCCESS);
}

TEST_F(CLITests, Failed_encoding_of_standard_input_names_it)
{
    EXPECT_CALL(*compressor_, encode(::testing::_, ::testing::_)).WillOnce(Return(false));

    EXPECT_NE(run({"encode", "-o", output_}), EXIT_SUCCESS);
    EXPECT_THAT(err_.str(), HasSubstr("Encoding standard input failed"));
}

TEST_F(CLITests, Unknown_option_fails)
{
    EXPECT_CALL(*compressor_, encode(::testing::_, ::testing::_)).Times(0);

    EXPECT_NE(run({"encode", "-i", input_, "-o", output_, "--level", "9"}), EXIT_SUCCESS);
}

TEST_F(CLITests, Threads_default_to_zero_meaning_all_cores)
{
    EXPECT_CALL(*compressor_, encode(::testing::_, ::testing::_)).WillOnce(Return(true));

    EXPECT_EQ(run({"encode", "-i", input_, "-o", output_}), EXIT_SUCCESS);
    EXPECT_EQ(threads_, 0U);
}

TEST_F(CLITests, Threads_are_passed_to_the_compressor)
{
    EXPECT_CALL(*compressor_, encode(::testing::_, ::testing::_)).WillOnce(Return(true));
    EXPECT_EQ(run({"encode", "-T", "3", "-i", input_, "-o", output_}), EXIT_SUCCESS);
    EXPECT_EQ(threads_, 3U);

    compressor_ = std::make_unique<MockICompressor>();
    EXPECT_CALL(*compressor_, decode(::testing::_, ::testing::_)).WillOnce(Return(true));
    EXPECT_EQ(run({"decode", "--threads", "1", "-i", input_, "-o", output_}), EXIT_SUCCESS);
    EXPECT_EQ(threads_, 1U);
}

TEST_F(CLITests, Invalid_thread_counts_fail)
{
    for (const char* threads : {"-1", "two", ""})
    {
        EXPECT_NE(run({"encode", "-T", threads, "-i", input_, "-o", output_}), EXIT_SUCCESS)
            << threads;
    }
    EXPECT_FALSE(threads_);
}

TEST_F(CLITests, Encode_accepts_a_folder_and_decode_does_not)
{
    EXPECT_CALL(*compressor_, encode(::testing::_, ::testing::_)).WillOnce(Return(true));
    EXPECT_EQ(run({"encode", "-i", dir_.string(), "-o", output_}), EXIT_SUCCESS);

    compressor_ = std::make_unique<MockICompressor>();
    EXPECT_CALL(*compressor_, decode(::testing::_, ::testing::_)).Times(0);
    EXPECT_NE(run({"decode", "-i", dir_.string(), "-o", output_}), EXIT_SUCCESS);
}
