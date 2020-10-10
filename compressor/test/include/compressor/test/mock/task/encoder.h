#ifndef MockIEncoderTask_h
#define MockIEncoderTask_h

#include <fstream>
#include <string>

#include <gmock/gmock.h>

#include <compressor/data/data.h>
#include <compressor/data/reader.h>
#include <compressor/data/writer.h>
#include <compressor/task/coder.h>
#include <compressor/task/encoder.h>
#include <compressor/engine/engine.h>

class MockIEncoderTask: public compressor::IEncoderTask {
public:
    MOCK_METHOD(std::string, input_file, (), (override));
    MOCK_METHOD(std::string, output_file, (), (override));
    MOCK_METHOD(compressor::IEngine&, engine, (), (override));
    MOCK_METHOD(compressor::DecodedData&, data, (), (override));
    MOCK_METHOD(compressor::IDataReader<compressor::DecodedData>&, reader, (), (override));
    MOCK_METHOD(compressor::IDataWriter<compressor::EncodedData>&, writer, (), (override));
};

#endif /* MockIEncoderTask_h */
