#ifndef StdioBuf_h
#define StdioBuf_h

#include <cstddef>
#include <cstdio>
#include <ios>
#include <streambuf>
#include <vector>

namespace compressor
{

inline constexpr std::size_t default_buffer_size = std::size_t{64} * 1024;

// libc++'s std::cin and std::cout reach stdio one character at a time, about 5x slower than a
// file; these move whole buffers through fread and fwrite.
class StdioInputBuf : public std::streambuf
{
public:
    explicit StdioInputBuf(std::FILE* file, std::size_t buffer_size = default_buffer_size);

protected:
    int_type underflow() override;
    std::streamsize xsgetn(char_type* bytes, std::streamsize count) override;

private:
    // Throws on a read error; std::istream turns that into badbit.
    std::streamsize read(char_type* bytes, std::streamsize count);

    std::FILE* file_;
    std::vector<char_type> buffer_;
};

class StdioOutputBuf : public std::streambuf
{
public:
    explicit StdioOutputBuf(std::FILE* file, std::size_t buffer_size = default_buffer_size);
    ~StdioOutputBuf() override;
    StdioOutputBuf(const StdioOutputBuf&) = delete;
    StdioOutputBuf& operator=(const StdioOutputBuf&) = delete;
    StdioOutputBuf(StdioOutputBuf&&) = delete;
    StdioOutputBuf& operator=(StdioOutputBuf&&) = delete;

protected:
    int_type overflow(int_type ch) override;
    std::streamsize xsputn(const char_type* bytes, std::streamsize count) override;
    int sync() override;

private:
    bool flush_buffer();

    std::FILE* file_;
    std::vector<char_type> buffer_;
};

} // namespace compressor

#endif /* StdioBuf_h */
