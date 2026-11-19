#ifndef MockIEngine_h
#define MockIEngine_h

#include <cstdint>
#include <expected>
#include <span>
#include <vector>

#include <gmock/gmock.h>

#include <compressor/data/data.h>
#include <compressor/engine/engine.h>
#include <compressor/error.h>

class MockIEngine : public compressor::IEngine
{
public:
    MOCK_METHOD(void, print_graph, (), (override, const));
    MOCK_METHOD(void, print_dict, (), (override, const));
    MOCK_METHOD(compressor::EncodedBlock, encode_block, (std::span<const uint8_t>), (override));
    MOCK_METHOD((std::expected<void, compressor::Error>), decode_block,
                (const compressor::EncodedBlock&, std::vector<uint8_t>&), (override));
};

#endif /* MockIEngine_h */
