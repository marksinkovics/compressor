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
    virtual ~DataWriter() = default;
    
    virtual bool open(const std::string& file)
    {
        output_stream_.open(file, std::ios::in | std::ios::binary);
        if (output_stream_.fail()) {
            std::cerr << "Cannot open file: " << file << '\n';
            return false;
        }
        return true;
    }

    virtual void write(const T& data)
    {
        output_stream_ << data;
    }

    virtual void close()
    {
        output_stream_.close();
    }
private:
    std::ofstream output_stream_;
};
    
} // compressor

#endif /* DataWriter_h */

