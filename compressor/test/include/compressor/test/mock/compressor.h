#ifndef MockICompressor_h
#define MockICompressor_h

#include <filesystem>
#include <string>
#include <vector>

#include <gmock/gmock.h>

#include <compressor/compressor.h>

class MockICompressor : public compressor::ICompressor
{
public:
    MOCK_METHOD(bool, encode, (const std::filesystem::path&, const std::filesystem::path&),
                (override));
    MOCK_METHOD(bool, decode, (const std::filesystem::path&, const std::filesystem::path&),
                (override));
    MOCK_METHOD(bool, encode_as,
                (const std::filesystem::path&, const std::filesystem::path&,
                 compressor::OutputFormat),
                (override));
    MOCK_METHOD(bool, list, (const std::filesystem::path&, bool), (override));
    MOCK_METHOD(bool, extract,
                (const std::filesystem::path&, const std::string&, const std::filesystem::path&),
                (override));
    MOCK_METHOD(bool, verify, (const std::filesystem::path&), (override));
    MOCK_METHOD(compressor::SearchResult, grep,
                (const std::filesystem::path&, const compressor::SearchOptions&,
                 const std::vector<std::string>&),
                (override));
    MOCK_METHOD(compressor::SearchResult, search,
                (const std::filesystem::path&, const std::string&, bool), (override));
    MOCK_METHOD(bool, stats, (const std::filesystem::path&, const compressor::StatsOptions&),
                (override));
};

#endif /* MockICompressor_h */
