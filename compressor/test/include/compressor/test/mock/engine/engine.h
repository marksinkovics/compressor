#ifndef MockIEngine_h
#define MockIEngine_h

#include <expected>

#include <gmock/gmock.h>

#include <compressor/compressor.h>
#include <compressor/engine/engine.h>
#include <compressor/error.h>

class MockIEngine : public compressor::IEngine
{
public:
    MOCK_METHOD(void, print_graph, (), (override, const));
    MOCK_METHOD(void, print_dict, (), (override, const));
    MOCK_METHOD(compressor::EncodedData, encode, (const compressor::DecodedData&), (override));
    MOCK_METHOD((std::expected<compressor::DecodedData, compressor::Error>), decode,
                (const compressor::EncodedData&), (override));
};

#endif /* MockIEngine_h */
