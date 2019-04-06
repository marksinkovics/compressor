#include <sstream>

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include <argparser/test/mock/argparser.h>

#include <compressor/test/mock/compressor.h>

#include <compressor/cli.h>

class CLITests : public ::testing::Test {
protected:
    void SetUp() override
    {
        argparser_ = std::make_unique<MockIArgparser>();
        compressor_ = std::make_unique<MockICompressor>();
    }

    std::unique_ptr<MockIArgparser> argparser_;
    std::unique_ptr<MockICompressor> compressor_;

    std::string input_file_ = "input.txt";
    std::string output_file_ = "output.txt";

    argparser::IArgparser::container_type options_ = {
        {"input", std::make_shared<argparser::Arg<std::string>>("input", "desc", input_file_)},
        {"output", std::make_shared<argparser::Arg<std::string>>("output", "desc", output_file_)}
    };
};


TEST_F(CLITests, Encoding)
{
    EXPECT_CALL(*argparser_, add_argument(::testing::_)).Times(testing::AtLeast(4));

    options_["encode"] = std::make_shared<argparser::Arg<bool>>("encode", "desc", true);

    EXPECT_CALL(*argparser_, parse()).WillOnce(testing::Return(options_));
    EXPECT_CALL(*argparser_, has_argument(testing::StrEq("input"))).WillOnce(testing::Return(true));
    EXPECT_CALL(*argparser_, has_argument(testing::StrEq("output"))).WillOnce(testing::Return(true));
    EXPECT_CALL(*argparser_, has_argument(testing::StrEq("encode"))).WillOnce(testing::Return(true));

    EXPECT_CALL(*compressor_, encode(testing::_)).Times(testing::AtLeast(1));

    auto cli = compressor::CLI(std::move(argparser_), std::move(compressor_));
    cli.run();
}

TEST_F(CLITests, Decoding)
{
    EXPECT_CALL(*argparser_, add_argument(::testing::_)).Times(testing::AtLeast(4));

    options_["decode"] = std::make_shared<argparser::Arg<bool>>("decode", "desc", true);

    EXPECT_CALL(*argparser_, parse()).WillOnce(testing::Return(options_));
    EXPECT_CALL(*argparser_, has_argument(testing::StrEq("input"))).WillOnce(testing::Return(true));
    EXPECT_CALL(*argparser_, has_argument(testing::StrEq("output"))).WillOnce(testing::Return(true));
    EXPECT_CALL(*argparser_, has_argument(testing::StrEq("encode"))).WillOnce(testing::Return(false));
    EXPECT_CALL(*argparser_, has_argument(testing::StrEq("decode"))).WillOnce(testing::Return(true));

    EXPECT_CALL(*compressor_, decode(testing::_)).Times(testing::AtLeast(1));

    auto cli = compressor::CLI(std::move(argparser_), std::move(compressor_));
    cli.run();
}

TEST_F(CLITests, EmptyCommand)
{
    EXPECT_CALL(*argparser_, add_argument(::testing::_)).Times(testing::AtLeast(4));

    EXPECT_CALL(*argparser_, parse()).WillOnce(testing::Return(options_));

    EXPECT_CALL(*argparser_, has_argument(testing::StrEq("input"))).WillOnce(testing::Return(true));
    EXPECT_CALL(*argparser_, has_argument(testing::StrEq("output"))).WillOnce(testing::Return(true));
    EXPECT_CALL(*argparser_, has_argument(testing::StrEq("encode"))).WillOnce(testing::Return(false));
    EXPECT_CALL(*argparser_, has_argument(testing::StrEq("decode"))).WillOnce(testing::Return(false));

    EXPECT_CALL(*compressor_, encode(testing::_)).Times(testing::AtLeast(0));

    EXPECT_CALL(*compressor_, decode(testing::_)).Times(testing::AtLeast(0));

    auto cli = compressor::CLI(std::move(argparser_), std::move(compressor_));
    cli.run();
}

TEST_F(CLITests, EmptyFiles)
{

}
