#ifndef MockIDataReader_h
#define MockIDataReader_h

#include <string>

#include <gmock/gmock.h>

#include <compressor/data/reader.h>

template <typename T>
class MockIDataReader : public compressor::IDataReader<T> {
public:
    MOCK_METHOD(bool, open, (const std::string& file), (override));
    MOCK_METHOD(T, read, (), (override));
    MOCK_METHOD(void, close, (), (override));
};

#endif /* MockIDataReader_h */
