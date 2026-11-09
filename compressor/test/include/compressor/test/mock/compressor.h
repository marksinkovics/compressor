#ifndef MockICompressor_h
#define MockICompressor_h

#include <gmock/gmock.h>

#include <compressor/compressor.h>
#include <compressor/task/decoder.h>
#include <compressor/task/encoder.h>

class MockICompressor : public compressor::ICompressor
{
public:
    MOCK_METHOD(bool, encode, (compressor::IEncoderTask&), (override));
    MOCK_METHOD(bool, decode, (compressor::IDecoderTask&), (override));
};

#endif /* MockICompressor_h */
