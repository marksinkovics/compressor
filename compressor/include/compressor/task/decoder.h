#ifndef DecoderTask_h
#define DecoderTask_h

#include <compressor/task/coder.h>
#include <compressor/data/data.h>
#include <compressor/data/reader.h>
#include <compressor/data/writer.h>

namespace compressor
{

class IDecoderTask: public ICoderTask
{
public:
    virtual ~IDecoderTask() = default;
    virtual EncodedData& data() = 0;
    virtual IDataReader<EncodedData>& reader() = 0;
    virtual IDataWriter<DecodedData>& writer() = 0;
};

class DecoderTask: public IDecoderTask
{
public:
    DecoderTask();
    DecoderTask(const std::string& input_file, const std::string& output_file);
    virtual ~DecoderTask() = default;
    // input
    virtual std::string input_file();
    // output
    virtual std::string output_file();
    // engine
    virtual IEngine& engine();
    // data
    virtual EncodedData& data();
    // reader
    virtual IDataReader<EncodedData>& reader();
    // writer
    virtual IDataWriter<DecodedData>& writer();
private:
    // input
    std::string input_file_;
    // output
    std::string output_file_;
    // engine
    std::shared_ptr<IEngine> engine_;
    // data
    EncodedData data_;
    // reader
    DataReader<EncodedData> reader_;
    // writer
    DataWriter<DecodedData> writer_;
};

} // compressor

#endif /* DecoderTask_h */
