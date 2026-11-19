#ifndef MockICompressor_h
#define MockICompressor_h

#include <filesystem>

#include <gmock/gmock.h>

#include <compressor/compressor.h>

class MockICompressor : public compressor::ICompressor
{
public:
    MOCK_METHOD(bool, encode, (const std::filesystem::path&, const std::filesystem::path&),
                (override));
    MOCK_METHOD(bool, decode, (const std::filesystem::path&, const std::filesystem::path&),
                (override));
};

#endif /* MockICompressor_h */
