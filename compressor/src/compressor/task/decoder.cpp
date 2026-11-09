#include <compressor/task/decoder.h>

#include <utility>

namespace compressor
{

DecoderTask::DecoderTask()
    : engine_(std::make_shared<Engine>())
{

}

DecoderTask::DecoderTask(std::string input_file, std::string output_file)
    : input_file_(std::move(input_file))
    , output_file_(std::move(output_file))
    , engine_(std::make_shared<Engine>())
{

}

// input
std::string DecoderTask::input_file()
{
    return input_file_;
}

// output
std::string DecoderTask::output_file()
{
    return output_file_;
}

// engine
IEngine& DecoderTask::engine()
{
    return *engine_;
}

// data
EncodedData& DecoderTask::data()
{
    return data_;
}
    
// reader
IDataReader<EncodedData>& DecoderTask::reader()
{
    return reader_;
}

// writer
IDataWriter<DecodedData>& DecoderTask::writer()
{
    
    return writer_;
}

} // compressor
