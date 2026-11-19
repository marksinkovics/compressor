#ifndef MockIBlockEncoder_h
#define MockIBlockEncoder_h

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
};

#endif /* MockIBlockEncoder_h */
