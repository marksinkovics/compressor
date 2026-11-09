#ifndef DataReader_h
#define DataReader_h

#include <compressor/data/data.h>

namespace compressor
{

template <class T> class IDataReader
{
public:
    virtual ~IDataReader() = default;
    virtual bool open(const std::string& file) = 0;
    virtual T read() = 0;
    virtual void close() = 0;
};

template <class T> class DataReader : public IDataReader<T>
{
public:
    ~DataReader() override = default;

    bool open(const std::string& file) override
    {
        input_stream_.open(file, std::ios::in | std::ios::binary);
        if (input_stream_.fail())
        {
            std::cerr << "Cannot open file: " << file << '\n';
            return false;
        }
        return true;
    }

    T read() override
    {
        T data;
        input_stream_ >> data;
        return data;
    }

    void close() override
    {
        input_stream_.close();
    }

private:
    std::ifstream input_stream_;
};

} // namespace compressor

#endif /* DataReader_h */
