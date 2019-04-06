#include <gmock/gmock.h>

#include <argparser/argparser.h>

class MockIArgparser : public argparser::IArgparser {
public:
    MOCK_METHOD1(add_argument, void(std::shared_ptr<argparser::BaseArg> arg));
    MOCK_METHOD0(parse, container_type());
    MOCK_CONST_METHOD1(has_argument, bool(const std::string& argument));
    MOCK_CONST_METHOD0(print_help, void());
};