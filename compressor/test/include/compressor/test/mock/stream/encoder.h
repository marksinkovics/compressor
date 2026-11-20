#ifndef MockIBlockEncoder_h
#define MockIBlockEncoder_h

#include <cstdint>
#include <expected>
#include <istream>
#include <ostream>

#include <gmock/gmock.h>

#include <compressor/error.h>
#include <compressor/stream/encoder.h>

class MockIBlockEncoder : public compressor::IBlockEncoder
{
public:
    MOCK_METHOD((std::expected<void, compressor::Error>), encode, (std::istream&, std::ostream&),
                (override));
    MOCK_METHOD((std::expected<compressor::FrameLayout, compressor::Error>), encode_frame,
                (std::istream&, std::ostream&, uint16_t), (override));
};

#endif /* MockIBlockEncoder_h */
