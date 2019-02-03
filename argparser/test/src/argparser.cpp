#include <gtest/gtest.h>
#include <argparser/argparser.h>

#include <string>
#include <map>

namespace argparser
{

class ArgParserTests : public ::testing::Test
{

};
    
TEST_F(ArgParserTests, has_argument)
{
    std::vector<char*> argv {
        (char*)"program_name",
        (char*)"--key1"
    };
    
    argparser::Argparser parser(argv.size(), &argv[0]);
    parser.add_argument("--key1", "desc", false);
    auto options = parser.parse();
    EXPECT_TRUE(parser.has_argument("key1"));
}
    
TEST_F(ArgParserTests, has_argument_with_equal_sign)
{
    std::vector<char*> argv {
        (char*)"program_name",
        (char*)"--key1=true"
    };
    
    argparser::Argparser parser(argv.size(), &argv[0]);
    parser.add_argument("--key1", "desc", false);
    auto options = parser.parse();
    EXPECT_TRUE(parser.has_argument("key1"));
}
    
TEST_F(ArgParserTests, has_multiple_arguments)
{
    std::vector<char*> argv {
        (char*)"program_name",
        (char*)"--key1=true",
        (char*)"--key2=false"
    };
    
    argparser::Argparser parser(argv.size(), &argv[0]);
    parser.add_argument("--key1", "desc", false);
    parser.add_argument("--key2", "desc", false);
    auto options = parser.parse();
    EXPECT_TRUE(parser.has_argument("key1"));
    EXPECT_TRUE(parser.has_argument("key2"));
}

TEST_F(ArgParserTests, value_bool)
{
    std::vector<char*> argv {
        (char*)"program_name",
        (char*)"--key1"
    };
    
    argparser::Argparser parser(argv.size(), &argv[0]);
    parser.add_argument("--key1", "desc", false);
    auto options = parser.parse();
    EXPECT_TRUE(std::dynamic_pointer_cast<Arg<bool>>(options["key1"])->value());
}

TEST_F(ArgParserTests, value_uint8_t)
{
    std::vector<char*> argv {
        (char*)"program_name",
        (char*)"--key1",
        (char*)"42"
    };
    
    argparser::Argparser parser(argv.size(), &argv[0]);
    parser.add_argument("--key1", "desc", uint8_t(0));
    auto options = parser.parse();
    EXPECT_EQ(42, std::dynamic_pointer_cast<Arg<uint8_t>>(options["key1"])->value());
}
    
TEST_F(ArgParserTests, value_uint16_t)
{
    std::vector<char*> argv {
        (char*)"program_name",
        (char*)"--key1",
        (char*)"42"
    };
    
    argparser::Argparser parser(argv.size(), &argv[0]);
    parser.add_argument("--key1", "desc", uint16_t(0));
    auto options = parser.parse();
    EXPECT_EQ(42, std::dynamic_pointer_cast<Arg<uint16_t>>(options["key1"])->value());
}

TEST_F(ArgParserTests, value_uint32_t)
{
    std::vector<char*> argv {
        (char*)"program_name",
        (char*)"--key1",
        (char*)"42"
    };
    
    argparser::Argparser parser(argv.size(), &argv[0]);
    parser.add_argument("--key1", "desc", uint32_t(0));
    auto options = parser.parse();
    EXPECT_EQ(uint32_t(42), std::dynamic_pointer_cast<Arg<uint32_t>>(options["key1"])->value());
}

TEST_F(ArgParserTests, value_uint64_t)
{
    std::vector<char*> argv {
        (char*)"program_name",
        (char*)"--key1",
        (char*)"42"
    };
    
    argparser::Argparser parser(argv.size(), &argv[0]);
    parser.add_argument("--key1", "desc", uint64_t(0));
    auto options = parser.parse();
    EXPECT_EQ(uint64_t(42), std::dynamic_pointer_cast<Arg<uint64_t>>(options["key1"])->value());
}
    
TEST_F(ArgParserTests, value_int)
{
    std::vector<char*> argv {
        (char*)"program_name",
        (char*)"--key1",
        (char*)"42",
        (char*)"--key2",
        (char*)"-42"
    };
    
    argparser::Argparser parser(argv.size(), &argv[0]);
    parser.add_argument("--key1", "desc", int(0));
    parser.add_argument("--key2", "desc", int(0));
    auto options = parser.parse();
    EXPECT_EQ(42, std::dynamic_pointer_cast<Arg<int>>(options["key1"])->value());
    EXPECT_EQ(-42, std::dynamic_pointer_cast<Arg<int>>(options["key2"])->value());
}


TEST_F(ArgParserTests, value_float)
{
    std::vector<char*> argv {
        (char*)"program_name",
        (char*)"--key1",
        (char*)"3.14"
    };
    
    argparser::Argparser parser(argv.size(), &argv[0]);
    parser.add_argument("--key1", "desc", float(0));
    auto options = parser.parse();
    EXPECT_FLOAT_EQ(3.14, std::dynamic_pointer_cast<Arg<float>>(options["key1"])->value());
}
    
TEST_F(ArgParserTests, value_double)
{
    std::vector<char*> argv {
        (char*)"program_name",
        (char*)"--key1",
        (char*)"3.14"
    };
    
    argparser::Argparser parser(argv.size(), &argv[0]);
    parser.add_argument("--key1", "desc", double(0));
    auto options = parser.parse();
    EXPECT_DOUBLE_EQ(3.14, std::dynamic_pointer_cast<Arg<double>>(options["key1"])->value());
}

TEST_F(ArgParserTests, value_string)
{
    std::vector<char*> argv {
        (char*)"program_name",
        (char*)"--key1",
        (char*)"sample_text"
    };
    
    argparser::Argparser parser(argv.size(), &argv[0]);
    parser.add_argument("--key1", "desc", std::string(""));
    auto options = parser.parse();
    EXPECT_STREQ("sample_text", std::dynamic_pointer_cast<Arg<std::string>>(options["key1"])->value().c_str());
}
    
TEST_F(ArgParserTests, value_varied_types)
{
    std::vector<char*> argv {
        (char*)"program_name",
        (char*)"--bool_key",
        (char*)"--str_key",
        (char*)"sample_text",
        (char*)"--int_key",
        (char*)"42",
        (char*)"--float_key",
        (char*)"3.14",
        (char*)"--double_key",
        (char*)"3.14"
    };

    argparser::Argparser parser(argv.size(), &argv[0]);
    parser.add_argument("--bool_key", "", bool(false));
    parser.add_argument("--str_key", "", std::string(""));
    parser.add_argument("--int_key", "", int(42));
    parser.add_argument("--float_key", "", float(3.14));
    parser.add_argument("--double_key", "", double(3.14));

    auto options = parser.parse();

    EXPECT_TRUE(std::dynamic_pointer_cast<Arg<bool>>(options["bool_key"])->value());
    EXPECT_STREQ("sample_text", std::dynamic_pointer_cast<Arg<std::string>>(options["str_key"])->value().c_str());
    EXPECT_EQ(42, std::dynamic_pointer_cast<Arg<int>>(options["int_key"])->value());
    EXPECT_FLOAT_EQ(3.14, std::dynamic_pointer_cast<Arg<float>>(options["float_key"])->value());
    EXPECT_DOUBLE_EQ(3.14, std::dynamic_pointer_cast<Arg<double>>(options["double_key"])->value());
}

} // argparser
