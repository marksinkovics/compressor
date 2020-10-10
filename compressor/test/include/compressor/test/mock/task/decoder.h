#ifndef MockIDecoderTask_h
#define MockIDecoderTask_h

#include <fstream>
#include <string>

#include <gmock/gmock.h>

#include <compressor/data/data.h>
#include <compressor/data/reader.h>
#include <compressor/data/writer.h>
#include <compressor/task/coder.h>
#include <compressor/task/decoder.h>
#include <compressor/engine/engine.h>

class MockIDecoderTask: public compressor::IDecoderTask {
public:
    MOCK_METHOD(std::string, input_file, (), (override));
    MOCK_METHOD(std::string, output_file, (), (override));
    MOCK_METHOD(compressor::IEngine&, engine, (), (override));
    MOCK_METHOD(compressor::EncodedData&, data, (), (override));
    MOCK_METHOD(compressor::IDataReader<compressor::EncodedData>&, reader, (), (override));
    MOCK_METHOD(compressor::IDataWriter<compressor::DecodedData>&, writer, (), (override));
};

#endif /* MockIDecoderTask_h */
