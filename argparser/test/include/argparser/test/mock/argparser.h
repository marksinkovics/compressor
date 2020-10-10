#include <gmock/gmock.h>

#include <argparser/argparser.h>

class MockIArgparser : public argparser::IArgparser {
public:
    MOCK_METHOD(void, add_argument, (std::shared_ptr<argparser::BaseArg>), (override));
    MOCK_METHOD(container_type, parse, (), (override));
    MOCK_METHOD(bool, has_argument, (const std::string& argument), (const, override));
    MOCK_METHOD(void, print_help, (), (const, override));
};
