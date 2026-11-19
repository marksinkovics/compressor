#ifndef MockIBlockDecoder_h
#define MockIBlockDecoder_h

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
};

#endif /* MockIBlockDecoder_h */
