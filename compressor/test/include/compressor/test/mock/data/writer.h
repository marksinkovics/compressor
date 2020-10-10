#ifndef MockIDataWriter_h
#define MockIDataWriter_h

#include <string>

#include <gmock/gmock.h>

#include <compressor/data/writer.h>

template <typename T>
class MockIDataWriter : public compressor::IDataWriter<T> {
public:
    MOCK_METHOD(bool, open, (const std::string&), (override));
    MOCK_METHOD(void, write, (const T&), (override));
    MOCK_METHOD(void, close, (), (override));
};

#endif /* MockIDataWriter_h */
