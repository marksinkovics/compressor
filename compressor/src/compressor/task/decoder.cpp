#include <compressor/task/decoder.h>

namespace compressor
{

DecoderTask::DecoderTask()
    : engine_(std::make_shared<Engine>())
{

}

DecoderTask::DecoderTask(const std::string& input_file, const std::string& output_file)
    : input_file_(input_file)
    , output_file_(output_file)
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
