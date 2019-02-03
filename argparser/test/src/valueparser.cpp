#include <gtest/gtest.h>
#include <argparser/argparser.h>

#include <any>
#include <string>
#include <map>

namespace argparser {

class ValueParserTests : public ::testing::Test {};

class BoolValueParserTests : public ValueParserTests, public ::testing::WithParamInterface<std::tuple<std::string, bool>> {};
    
INSTANTIATE_TEST_CASE_P(TrueValueParserTestsParametersTrue, BoolValueParserTests, ::testing::Values(
    std::make_tuple("true", true),
    std::make_tuple("True", true),
    std::make_tuple("t", true),
    std::make_tuple("T", true),
    std::make_tuple("yes", true),
    std::make_tuple("Yes", true),
    std::make_tuple("y", true),
    std::make_tuple("T", true)
));

INSTANTIATE_TEST_CASE_P(FalseValueParserTestsParametersTrue, BoolValueParserTests, ::testing::Values(
    std::make_tuple("false", false),
    std::make_tuple("False", false),
    std::make_tuple("f", false),
    std::make_tuple("F", false),
    std::make_tuple("no", false),
    std::make_tuple("No", false),
    std::make_tuple("n", false),
    std::make_tuple("N", false)
));

    
TEST_P(BoolValueParserTests, Bool)
{
    std::string str = std::get<0>( GetParam());
    bool expected = std::get<1>( GetParam());
    EXPECT_EQ(expected, ValueParser<bool>::parse(str));
}

TEST_F(ValueParserTests, Int)
{
    EXPECT_EQ(uint8_t(42), ValueParser<uint8_t>::parse("42"));
    EXPECT_EQ(uint16_t(42), ValueParser<uint16_t>::parse("42"));
    EXPECT_EQ(uint32_t(42), ValueParser<uint32_t>::parse("42"));
    EXPECT_EQ(uint64_t(42), ValueParser<uint64_t>::parse("42"));
}
    
TEST_F(ValueParserTests, Float)
{
    EXPECT_FLOAT_EQ(3.14, ValueParser<float>::parse("3.14"));
}
    
TEST_F(ValueParserTests, Double)
{
    EXPECT_DOUBLE_EQ(3.14, ValueParser<double>::parse("3.14"));
}

struct SampleStruct {
    int v1;
    float v2;
};
    
template<> SampleStruct ValueParser<SampleStruct>::parse(const std::string& str)
{
    std::stringstream stream(str);
    int v1;
    float v2;
    stream >> v1 >> v2;
    SampleStruct result;
    result.v1 = v1;
    result.v2 = v2;
    return result;
}

TEST_F(ValueParserTests, Custom)
{
    std::stringstream stream;
    stream << 42 << '\n';
    stream << 3.14;
    auto result = ValueParser<SampleStruct>::parse(stream.str());
    EXPECT_EQ(42, result.v1);
    EXPECT_FLOAT_EQ(3.14, result.v2);
}
    
} // argparser
