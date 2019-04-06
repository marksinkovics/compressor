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
    virtual ~IEncoderTask() = default;
    virtual DecodedData& data() = 0;
    virtual IDataReader<DecodedData>& reader() = 0;
    virtual IDataWriter<EncodedData>& writer() = 0;
};

class EncoderTask: public IEncoderTask
{
public:
    EncoderTask();
    EncoderTask(const std::string& input_file, const std::string& output_file);
    virtual ~EncoderTask() = default;
    // input
    virtual std::string input_file();
    // output
    virtual std::string output_file();
    // engine
    virtual IEngine& engine();
    // data
    virtual DecodedData& data();
    // reader
    virtual IDataReader<DecodedData>& reader();
    // writer
    virtual IDataWriter<EncodedData>& writer();
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
