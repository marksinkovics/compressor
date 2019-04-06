#ifndef MockIDataWriter_h
#define MockIDataWriter_h

#include <string>

#include <gmock/gmock.h>

#include <compressor/data/writer.h>

template <typename T>
class MockIDataWriter : public compressor::IDataWriter<T> {
public:
    MOCK_METHOD1_T(open, bool(const std::string& file));
    MOCK_METHOD1_T(write, void(const T& data));
    MOCK_METHOD0_T(close, void());
};

#endif /* MockIDataWriter_h */
