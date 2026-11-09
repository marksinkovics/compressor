#include <compressor/task/encoder.h>

#include <utility>

namespace compressor
{

EncoderTask::EncoderTask()
    : engine_(std::make_shared<Engine>())
{
}

EncoderTask::EncoderTask(std::string input_file, std::string output_file)
    : input_file_(std::move(input_file))
    , output_file_(std::move(output_file))
    , engine_(std::make_shared<Engine>())
{
}

// input
std::string EncoderTask::input_file()
{
    return input_file_;
}

// output
std::string EncoderTask::output_file()
{
    return output_file_;
}

// engine
IEngine& EncoderTask::engine()
{
    return *engine_;
}

// data
DecodedData& EncoderTask::data()
{
    return data_;
}

// reader
IDataReader<DecodedData>& EncoderTask::reader()
{
    return reader_;
}

// writer
IDataWriter<EncodedData>& EncoderTask::writer()
{

    return writer_;
}

} // namespace compressor
