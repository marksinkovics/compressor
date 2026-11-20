#ifndef StdioBuf_h
#define StdioBuf_h

#include <cstddef>
#include <cstdio>
#include <filesystem>
#include <ios>
#include <memory>
#include <ostream>
#include <streambuf>
#include <vector>

namespace compressor
{

inline constexpr std::size_t default_buffer_size = std::size_t{64} * 1024;

// For files: writes as large as `dd bs=1m`'s.
inline constexpr std::size_t file_buffer_bytes = std::size_t{1} << 20U;

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

// fopen(path, "rb"); null with errno set on failure.
std::FILE* open_for_reading(const std::filesystem::path& path);

// A new file written through fwrite. libc++'s std::filebuf writes 4 KiB per system call, and a
// filebuf reopened after close() writes almost byte by byte; fwrite of whole buffers is 5-10x
// faster than either.
class OutputFile
{
public:
    // Creates `path`, failing (null, with errno set) if it already exists.
    static std::unique_ptr<OutputFile> create(const std::filesystem::path& path);
    ~OutputFile();
    OutputFile(const OutputFile&) = delete;
    OutputFile& operator=(const OutputFile&) = delete;
    OutputFile(OutputFile&&) = delete;
    OutputFile& operator=(OutputFile&&) = delete;

    std::ostream& stream();
    std::streambuf& buffer();
    // Flushes and closes; false on a write error. Only once.
    bool close();

private:
    explicit OutputFile(std::FILE* file);

    std::FILE* file_;
    StdioOutputBuf buffer_;
    std::ostream stream_;
};

} // namespace compressor

#endif /* StdioBuf_h */
