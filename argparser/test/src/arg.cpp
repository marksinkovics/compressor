#include <gtest/gtest.h>
#include <argparser/arg.h>

#include <any>
#include <string>
#include <map>
#include <vector>

namespace argparser
{

class ArgTests : public ::testing::Test
{
    
};

TEST_F(ArgTests, BasicInit)
{
    argparser::Arg arg1("--upload", "Description for upload", std::string(""));
    EXPECT_STREQ("--upload", arg1.arg().c_str());
    EXPECT_STREQ("Description for upload", arg1.description().c_str());
    EXPECT_STREQ("", arg1.value().c_str());
}

TEST_F(ArgTests, VariedArgsInit)
{
    argparser::Arg arg1("--upload", "Description for upload", std::string(""));
    EXPECT_STREQ("", arg1.value().c_str());
    
    argparser::Arg arg2("--upload", "Description for upload", uint8_t(42));
    EXPECT_EQ(uint8_t(42), arg2.value());

    argparser::Arg arg3("--upload", "Description for upload", bool(true));
    EXPECT_TRUE(arg3.value());
}

} // argparser
