#include <compressor/stream/stdio.h>

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <ios>
#include <streambuf>
#include <utility>

namespace compressor
{

StdioInputBuf::StdioInputBuf(std::FILE* file, std::size_t buffer_size)
    : file_(file)
    , buffer_(buffer_size)
{
    setg(buffer_.data(), buffer_.data(), buffer_.data());
}

std::streamsize StdioInputBuf::read(char_type* bytes, std::streamsize count)
{
    const std::size_t got = std::fread(bytes, 1, static_cast<std::size_t>(count), file_);
    if (std::cmp_less(got, count) && std::ferror(file_) != 0)
    {
        throw std::ios_base::failure("read failed");
    }
    return static_cast<std::streamsize>(got);
}

StdioInputBuf::int_type StdioInputBuf::underflow()
{
    if (gptr() == egptr())
    {
        const std::streamsize got =
            read(buffer_.data(), static_cast<std::streamsize>(buffer_.size()));
        setg(buffer_.data(), buffer_.data(), buffer_.data() + got);
        if (got == 0)
        {
            return traits_type::eof();
        }
    }
    return traits_type::to_int_type(*gptr());
}

std::streamsize StdioInputBuf::xsgetn(char_type* bytes, std::streamsize count)
{
    const std::streamsize buffered = std::min(count, egptr() - gptr());
    std::copy_n(gptr(), buffered, bytes);
    setg(eback(), gptr() + buffered, egptr());
    const std::streamsize rest = count - buffered;
    if (rest == 0)
    {
        return count;
    }
    if (std::cmp_greater_equal(rest, buffer_.size()))
    {
        return buffered + read(bytes + buffered, rest);
    }
    return buffered + std::streambuf::xsgetn(bytes + buffered, rest);
}

StdioOutputBuf::StdioOutputBuf(std::FILE* file, std::size_t buffer_size)
    : file_(file)
    , buffer_(buffer_size)
{
    setp(buffer_.data(), buffer_.data() + buffer_.size());
}

StdioOutputBuf::~StdioOutputBuf()
{
    flush_buffer();
}

bool StdioOutputBuf::flush_buffer()
{
    const auto pending = static_cast<std::size_t>(pptr() - pbase());
    const bool written = std::fwrite(pbase(), 1, pending, file_) == pending;
    setp(buffer_.data(), buffer_.data() + buffer_.size());
    return written;
}

StdioOutputBuf::int_type StdioOutputBuf::overflow(int_type ch)
{
    if (!flush_buffer())
    {
        return traits_type::eof();
    }
    if (!traits_type::eq_int_type(ch, traits_type::eof()))
    {
        *pptr() = traits_type::to_char_type(ch);
        pbump(1);
    }
    return traits_type::not_eof(ch);
}

std::streamsize StdioOutputBuf::xsputn(const char_type* bytes, std::streamsize count)
{
    if (count <= epptr() - pptr())
    {
        std::copy_n(bytes, count, pptr());
        pbump(static_cast<int>(count));
        return count;
    }
    if (!flush_buffer())
    {
        return 0;
    }
    if (std::cmp_greater_equal(count, buffer_.size()))
    {
        return static_cast<std::streamsize>(
            std::fwrite(bytes, 1, static_cast<std::size_t>(count), file_));
    }
    std::copy_n(bytes, count, pptr());
    pbump(static_cast<int>(count));
    return count;
}

int StdioOutputBuf::sync()
{
    return flush_buffer() && std::fflush(file_) == 0 ? 0 : -1;
}

} // namespace compressor
