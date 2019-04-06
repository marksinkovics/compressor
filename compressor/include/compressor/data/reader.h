#ifndef DataReader_h
#define DataReader_h

#include <compressor/data/data.h>

namespace compressor
{

template<class T>
class IDataReader
{
public:
    virtual ~IDataReader() = default;
    virtual bool open(const std::string& file) = 0;
    virtual T read() = 0;
    virtual void close() = 0;
};

template<class T>
class DataReader: public IDataReader<T>
{
public:
    virtual ~DataReader() = default;
    
    virtual bool open(const std::string& file)
    {
        input_stream_.open(file, std::ios::in | std::ios::binary);
        if (input_stream_.fail()) {
            std::cerr << "Cannot open file: " << file << '\n';
            return false;
        }
        return true;
    }
    
    virtual T read()
    {
        T data;
        input_stream_ >> data;
        return data;
    }
    
    virtual void close()
    {
        input_stream_.close();
    }
private:
    std::ifstream input_stream_;
};

} // compressor

#endif /* DataReader_h */

