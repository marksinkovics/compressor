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
    MOCK_METHOD0(input_file, std::string());
    MOCK_METHOD0(output_file, std::string());
    MOCK_METHOD0(engine, compressor::IEngine&());
    MOCK_METHOD0(data, compressor::EncodedData&());
    MOCK_METHOD0(reader, compressor::IDataReader<compressor::EncodedData>&());
    MOCK_METHOD0(writer, compressor::IDataWriter<compressor::DecodedData>&());
};

#endif /* MockIDecoderTask_h */
