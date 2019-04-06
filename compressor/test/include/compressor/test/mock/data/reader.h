#ifndef MockIDataReader_h
#define MockIDataReader_h

#include <string>

#include <gmock/gmock.h>

#include <compressor/data/reader.h>

template <typename T>
class MockIDataReader : public compressor::IDataReader<T> {
public:
    MOCK_METHOD1_T(open, bool(const std::string& file));
    MOCK_METHOD0_T(read, T());
    MOCK_METHOD0_T(close, void());
};

#endif /* MockIDataReader_h */
