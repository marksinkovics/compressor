#ifndef MockICompressor_h
#define MockICompressor_h

#include <gmock/gmock.h>

#include <compressor/compressor.h>
#include <compressor/task/encoder.h>
#include <compressor/task/decoder.h>

class MockICompressor : public compressor::ICompressor {
public:
    MOCK_METHOD1(encode, void(compressor::IEncoderTask& task));
    MOCK_METHOD1(decode, void(compressor::IDecoderTask& task));
};

#endif /* MockICompressor_h */
