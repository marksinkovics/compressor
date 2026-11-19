#ifndef MockIDataReader_h
#define MockIDataReader_h

#include <expected>
#include <string>

#include <gmock/gmock.h>

#include <compressor/data/reader.h>
#include <compressor/error.h>

template <typename T> class MockIDataReader : public compressor::IDataReader<T>
{
public:
    MOCK_METHOD(bool, open, (const std::string& file), (override));
    MOCK_METHOD((std::expected<T, compressor::Error>), read, (), (override));
    MOCK_METHOD(void, close, (), (override));
};

#endif /* MockIDataReader_h */
