#ifndef MockICompressor_h
#define MockICompressor_h

#include <gmock/gmock.h>

#include <compressor/compressor.h>
#include <compressor/task/encoder.h>
#include <compressor/task/decoder.h>

class MockICompressor : public compressor::ICompressor {
public:
    MOCK_METHOD(void, encode, (compressor::IEncoderTask&), (override));
    MOCK_METHOD(void, decode, (compressor::IDecoderTask&), (override));
};

#endif /* MockICompressor_h */
