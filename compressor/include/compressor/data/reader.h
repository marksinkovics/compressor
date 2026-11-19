#ifndef DataReader_h
#define DataReader_h

#include <compressor/data/data.h>
#include <compressor/error.h>

#include <expected>
#include <fstream>
#include <string>

namespace compressor
{

template <class T> class IDataReader
{
public:
    virtual ~IDataReader() = default;
    virtual bool open(const std::string& file) = 0;
    virtual std::expected<T, Error> read() = 0;
    virtual void close() = 0;
};

template <class T> class DataReader : public IDataReader<T>
{
public:
    ~DataReader() override = default;

    bool open(const std::string& file) override
    {
        input_stream_.open(file, std::ios::in | std::ios::binary);
        return !input_stream_.fail();
    }

    // operator>> sets failbit only when the data it read is invalid.
    std::expected<T, Error> read() override
    {
        T data;
        input_stream_ >> data;
        if (input_stream_.bad())
        {
            return std::unexpected(Error::read_failed);
        }
        if (input_stream_.fail())
        {
            return std::unexpected(Error::corrupt_header);
        }
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
