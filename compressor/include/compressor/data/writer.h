#ifndef DataWriter_h
#define DataWriter_h

#include <compressor/data/data.h>

namespace compressor
{

template<class T>
class IDataWriter
{
public:
    virtual ~IDataWriter() = default;
    virtual bool open(const std::string& file) = 0;
    virtual void write(const T& data) = 0;
    virtual void close() = 0;
};

template<class T>
class DataWriter: public IDataWriter<T>
{
public:
    ~DataWriter() override = default;

    bool open(const std::string& file) override
    {
        output_stream_.open(file, std::ios::in | std::ios::binary);
        if (output_stream_.fail()) {
            std::cerr << "Cannot open file: " << file << '\n';
            return false;
        }
        return true;
    }

    void write(const T& data) override
    {
        output_stream_ << data;
    }

    void close() override
    {
        output_stream_.close();
    }
private:
    std::ofstream output_stream_;
};
    
} // compressor

#endif /* DataWriter_h */

