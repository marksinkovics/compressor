#include <gtest/gtest.h>
#include <argparser/argparser.h>

#include <any>
#include <string>
#include <map>

class ArgParserTests : public ::testing::Test
{

};

TEST_F(ArgParserTests, StringArguments)
{
    std::vector<char*> argv {
        (char*)"program_name",
        (char*)"key1",
        (char*)"value1",
        (char*)"key2=value2"
    };
    
    argparser::Argparser parser(argv.size(), &argv[0]);
    parser.add_argument("key1", std::string(), "Description for key1");
    parser.add_argument("key2", std::string(), "Description for key2");
    auto options = parser.parse();

    EXPECT_TRUE(parser.has_argument("key1"));
    EXPECT_TRUE(parser.has_argument("key2"));
    
    EXPECT_STREQ("value1", std::any_cast<std::string>(options["key1"]).c_str());
    EXPECT_STREQ("value2", std::any_cast<std::string>(options["key2"]).c_str());
}

TEST_F(ArgParserTests, VariedArguments)
{
    std::vector<char*> argv {
        (char*)"alma",
        (char*)"key1",
        (char*)"value1",
        (char*)"key2",
        (char*)"key3",
        (char*)"1",
        (char*)"key4",
        (char*)"3.14",
        (char*)"key5=korte",
        (char*)"key6=2"
    };
    
    argparser::Argparser parser(argv.size(), &argv[0]);
    parser.add_argument("key1", std::string(), "Description for key1");
    parser.add_argument("key2", bool(false), "Description for key2");
    parser.add_argument("key3", int(0), "Description for key3");
    parser.add_argument("key4", float(0), "Description for key4");
    parser.add_argument("key5", std::string(), "Description for key5");
    parser.add_argument("key6", int(0), "Description for key6");
    auto options = parser.parse();
    
    EXPECT_TRUE(parser.has_argument("key1"));
    EXPECT_TRUE(parser.has_argument("key2"));
    EXPECT_TRUE(parser.has_argument("key3"));
    EXPECT_TRUE(parser.has_argument("key4"));
    EXPECT_TRUE(parser.has_argument("key5"));
    EXPECT_TRUE(parser.has_argument("key6"));

    EXPECT_STREQ("value1", std::any_cast<std::string>(options["key1"]).c_str());
    EXPECT_TRUE(std::any_cast<bool>(options["key2"]));
    EXPECT_EQ(1, std::any_cast<int>(options["key3"]));
    EXPECT_NEAR(3.14, std::any_cast<float>(options["key4"]), 0.1);
    EXPECT_STREQ("korte", std::any_cast<std::string>(options["key5"]).c_str());
    EXPECT_EQ(2, std::any_cast<int>(options["key6"]));
}

TEST_F(ArgParserTests, Help1)
{
    std::vector<char*> argv {
        (char*)"program_name",
        (char*)"-h"
    };
    
    argparser::Argparser parser(argv.size(), &argv[0]);
    parser.add_argument("key1", std::string(), "Description for key1");
    parser.add_argument("key2", std::string(), "Description for key2");
    
    testing::internal::CaptureStdout();
    
    parser.parse();
    
    std::string help_message = ::testing::internal::GetCapturedStdout();
    std::stringstream expected_help_message;
    expected_help_message << "Usage: program_name [options]\n\n";
    expected_help_message << "\tkey1\t\t\tDescription for key1\n";
    expected_help_message << "\tkey2\t\t\tDescription for key2\n";
    expected_help_message << "\n\t-h, --help\t\tshow this help message and exit\n";
    ASSERT_STREQ(help_message.c_str(), expected_help_message.str().c_str());

}

TEST_F(ArgParserTests, Help2)
{
    std::vector<char*> argv {
        (char*)"program_name",
        (char*)"--help"
    };
    
    argparser::Argparser parser(argv.size(), &argv[0]);
    parser.add_argument("key1", std::string(), "Description for key1");
    parser.add_argument("key2", std::string(), "Description for key2");
    
    testing::internal::CaptureStdout();
    
    parser.parse();
    
    std::string help_message = ::testing::internal::GetCapturedStdout();
    std::stringstream expected_help_message;
    expected_help_message << "Usage: program_name [options]\n\n";
    expected_help_message << "\tkey1\t\t\tDescription for key1\n";
    expected_help_message << "\tkey2\t\t\tDescription for key2\n";
    expected_help_message << "\n\t-h, --help\t\tshow this help message and exit\n";
    ASSERT_STREQ(help_message.c_str(), expected_help_message.str().c_str());
}

