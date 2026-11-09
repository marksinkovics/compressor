#ifndef EncoderTask_h
#define EncoderTask_h

#include <compressor/task/coder.h>
#include <compressor/data/data.h>
#include <compressor/data/reader.h>
#include <compressor/data/writer.h>

namespace compressor
{

class IEncoderTask: public ICoderTask
{
public:
    ~IEncoderTask() override = default;
    virtual DecodedData& data() = 0;
    virtual IDataReader<DecodedData>& reader() = 0;
    virtual IDataWriter<EncodedData>& writer() = 0;
};

class EncoderTask: public IEncoderTask
{
public:
    EncoderTask();
    EncoderTask(std::string input_file, std::string output_file);
    ~EncoderTask() override = default;
    // input
    std::string input_file() override;
    // output
    std::string output_file() override;
    // engine
    IEngine& engine() override;
    // data
    DecodedData& data() override;
    // reader
    IDataReader<DecodedData>& reader() override;
    // writer
    IDataWriter<EncodedData>& writer() override;

private:
    // input
    std::string input_file_;
    // output
    std::string output_file_;
    // engine
    std::shared_ptr<IEngine> engine_;
    // data
    DecodedData data_;
    // reader
    DataReader<DecodedData> reader_;
    // writer
    DataWriter<EncodedData> writer_;
};

} // compressor

#endif /* EncoderTask_h */
