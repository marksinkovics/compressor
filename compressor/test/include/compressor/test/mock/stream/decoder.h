#ifndef MockIBlockDecoder_h
#define MockIBlockDecoder_h

#include <cstdint>
#include <expected>
#include <istream>
#include <ostream>

#include <gmock/gmock.h>

#include <compressor/error.h>
#include <compressor/stream/decoder.h>

class MockIBlockDecoder : public compressor::IBlockDecoder
{
public:
    MOCK_METHOD((std::expected<void, compressor::Error>), decode, (std::istream&, std::ostream&),
                (override));
    MOCK_METHOD((std::expected<void, compressor::Error>), decode_frame,
                (std::istream&, std::ostream&, uint16_t), (override));
};

#endif /* MockIBlockDecoder_h */
