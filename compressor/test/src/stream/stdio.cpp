#include <cstddef>
#include <cstdio>
#include <filesystem>
#include <istream>
#include <iterator>
#include <memory>
#include <ostream>
#include <string>

#include <gtest/gtest.h>

#include <compressor/stream/stdio.h>

namespace
{

namespace fs = std::filesystem;

constexpr std::size_t buffer_size = 16;

struct FileCloser
{
    void operator()(std::FILE* file) const
    {
        std::fclose(file);
    }
};
using File = std::unique_ptr<std::FILE, FileCloser>;

std::string pattern(std::size_t size)
{
    std::string bytes(size, '\0');
    for (std::size_t i = 0; i < size; ++i)
    {
        bytes[i] = static_cast<char>(i * 7 + 3);
    }
    return bytes;
}

} // namespace

class StdioBufTests : public ::testing::Test
{
protected:
    void SetUp() override
    {
        const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
        path_ = fs::temp_directory_path() / (std::string("compressor_stdio_") + info->name());
    }

    void TearDown() override
    {
        std::error_code error;
        fs::remove(path_, error);
    }

    File open(const char* mode) const
    {
        return File(std::fopen(path_.string().c_str(), mode));
    }

    void write_through_buf(const std::string& bytes, std::size_t piece) const
    {
        const File file = open("wb");
        compressor::StdioOutputBuf buf(file.get(), buffer_size);
        std::ostream out(&buf);
        for (std::size_t at = 0; at < bytes.size(); at += piece)
        {
            const std::string part = bytes.substr(at, piece);
            out.write(part.data(), static_cast<std::streamsize>(part.size()));
        }
        out.flush();
        EXPECT_TRUE(out.good());
    }

    std::string read_through_buf(std::size_t piece) const
    {
        const File file = open("rb");
        compressor::StdioInputBuf buf(file.get(), buffer_size);
        std::istream in(&buf);
        std::string bytes;
        std::string part(piece, '\0');
        while (in.read(part.data(), static_cast<std::streamsize>(piece)) || in.gcount() > 0)
        {
            bytes.append(part, 0, static_cast<std::size_t>(in.gcount()));
        }
        EXPECT_FALSE(in.bad());
        return bytes;
    }

    fs::path path_;
};

TEST_F(StdioBufTests, Round_trips_in_pieces_smaller_equal_and_larger_than_the_buffer)
{
    const std::string bytes = pattern(1000);
    for (const std::size_t piece : {std::size_t{1}, std::size_t{5}, buffer_size, buffer_size + 1,
                                    std::size_t{100}, std::size_t{4096}})
    {
        write_through_buf(bytes, piece);
        EXPECT_EQ(read_through_buf(piece), bytes) << "piece " << piece;
        for (const std::size_t other : {std::size_t{3}, std::size_t{64}})
        {
            EXPECT_EQ(read_through_buf(other), bytes) << "write " << piece << ", read " << other;
        }
    }
}

TEST_F(StdioBufTests, Characters_and_peek_mix_with_block_reads)
{
    write_through_buf(pattern(100), 100);
    const File file = open("rb");
    compressor::StdioInputBuf buf(file.get(), buffer_size);
    std::istream in(&buf);

    EXPECT_EQ(in.peek(), static_cast<unsigned char>(pattern(1)[0]));
    std::string bytes(1, static_cast<char>(in.get()));
    std::string rest(99, '\0');
    in.read(rest.data(), 99);
    bytes += rest;
    EXPECT_EQ(bytes, pattern(100));
    EXPECT_EQ(in.peek(), std::istream::traits_type::eof());
}

TEST_F(StdioBufTests, An_empty_input_reads_nothing)
{
    write_through_buf("", 1);
    EXPECT_EQ(read_through_buf(8), "");
}

TEST_F(StdioBufTests, Destroying_the_output_buffer_flushes_it)
{
    {
        const File file = open("wb");
        compressor::StdioOutputBuf buf(file.get(), buffer_size);
        std::ostream(&buf) << "abc";
    }
    EXPECT_EQ(read_through_buf(8), "abc");
}

TEST_F(StdioBufTests, A_read_error_sets_badbit)
{
    const File file = open("wb");
    compressor::StdioInputBuf buf(file.get(), buffer_size);
    std::istream in(&buf);
    char byte = 0;

    in.read(&byte, 1);
    EXPECT_TRUE(in.bad());
}

TEST_F(StdioBufTests, A_write_error_fails_the_stream)
{
    write_through_buf("abc", 3);
    const File file = open("rb");
    compressor::StdioOutputBuf buf(file.get(), buffer_size);
    std::ostream out(&buf);

    out << pattern(100);
    out.flush();
    EXPECT_TRUE(out.fail());
}
