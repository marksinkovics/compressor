#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>
#include <zlib.h>

#include <compressor/deflate/deflate.h>
#include <compressor/lz77/lz77.h>

namespace
{

namespace fs = std::filesystem;

// zlib's inflate; `window_bits` -15 for raw Deflate, 31 for gzip.
std::string inflate_with_zlib(const std::string& compressed, int window_bits,
                              gz_header* header = nullptr)
{
    z_stream stream{};
    EXPECT_EQ(inflateInit2(&stream, window_bits), Z_OK);
    if (header != nullptr)
    {
        EXPECT_EQ(inflateGetHeader(&stream, header), Z_OK);
    }
    std::string out;
    std::vector<char> buffer(std::size_t{1} << 16U);
    stream.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(compressed.data()));
    stream.avail_in = static_cast<uInt>(compressed.size());
    int status = Z_OK;
    while (status == Z_OK)
    {
        stream.next_out = reinterpret_cast<Bytef*>(buffer.data());
        stream.avail_out = static_cast<uInt>(buffer.size());
        status = inflate(&stream, Z_NO_FLUSH);
        out.append(buffer.data(), buffer.size() - stream.avail_out);
    }
    EXPECT_EQ(status, Z_STREAM_END) << (stream.msg != nullptr ? stream.msg : "");
    EXPECT_EQ(stream.avail_in, 0U) << "bytes after the end of the stream";
    inflateEnd(&stream);
    return out;
}

std::string deflate(const std::string& input, const compressor::Lz77Options& options = {})
{
    std::istringstream in(input);
    std::ostringstream out;
    const auto result = compressor::deflate_stream(in, out, options);
    EXPECT_TRUE(result);
    if (result)
    {
        EXPECT_EQ(result->input_bytes, input.size());
        EXPECT_EQ(result->output_bytes, out.str().size());
    }
    return out.str();
}

std::string noise(std::size_t size, uint32_t seed = 1)
{
    std::string bytes(size, '\0');
    for (char& c : bytes)
    {
        seed = (seed * 1103515245U) + 12345U;
        c = static_cast<char>(seed >> 16U);
    }
    return bytes;
}

std::string text(std::size_t size)
{
    static const std::vector<std::string> words{"alpha ",   "beta ",  "gamma\n", "delta ",
                                                "epsilon ", "zeta, ", "eta. "};
    std::string out;
    uint32_t state = 7;
    while (out.size() < size)
    {
        state = (state * 1103515245U) + 12345U;
        out += words[(state >> 16U) % words.size()];
    }
    out.resize(size);
    return out;
}

std::string read_file(const fs::path& path)
{
    std::ifstream file(path, std::ios::in | std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

} // namespace

TEST(DeflateTests, Small_inputs_inflate_back)
{
    for (const std::string& input : {std::string{}, std::string("a"), std::string("abc"),
                                     std::string("abcabcabcabcabc"), std::string(1000, 'z')})
    {
        EXPECT_EQ(inflate_with_zlib(deflate(input), -15), input) << input.size();
    }
}

TEST(DeflateTests, Text_shrinks_and_inflates_back)
{
    const std::string input = text(200'000);
    const std::string compressed = deflate(input);
    EXPECT_LT(compressed.size(), input.size() / 3);
    EXPECT_EQ(inflate_with_zlib(compressed, -15), input);
}

TEST(DeflateTests, Random_bytes_are_stored_and_inflate_back)
{
    const std::string input = noise(200'003);
    const std::string compressed = deflate(input);
    EXPECT_LT(compressed.size(), input.size() + 100);
    EXPECT_EQ(inflate_with_zlib(compressed, -15), input);
}

TEST(DeflateTests, Matches_reach_back_across_pieces_and_the_whole_window)
{
    // A random chunk repeating every 32 768 bytes, the farthest a match may reach, across the
    // 1 MiB pieces the input is read in.
    const std::string chunk = noise((32 * 1024) - 1000, 9);
    std::string input;
    while (input.size() < (3 * 1024 * 1024) + 5)
    {
        input += chunk;
        input += noise(1000, static_cast<uint32_t>(input.size()));
    }
    const std::string compressed = deflate(input);
    EXPECT_LT(compressed.size(), input.size() / 2);
    EXPECT_EQ(inflate_with_zlib(compressed, -15), input);
}

TEST(DeflateTests, Long_runs_use_the_longest_matches)
{
    std::string input(100'000, 'x');
    input += text(1000);
    input += std::string(70'000, '\0');
    const std::string compressed = deflate(input);
    EXPECT_LT(compressed.size(), 1000U);
    EXPECT_EQ(inflate_with_zlib(compressed, -15), input);
}

TEST(DeflateTests, Every_option_inflates_back)
{
    const std::string input = text(50'000) + noise(5000) + text(50'000);
    for (const compressor::Lz77Options& options :
         {compressor::Lz77Options{.max_chain = 1, .nice_length = 8, .lazy = false},
          compressor::Lz77Options{.max_chain = 4096, .nice_length = 258, .lazy = true}})
    {
        EXPECT_EQ(inflate_with_zlib(deflate(input, options), -15), input);
    }
}

TEST(DeflateTests, Sample_files_inflate_back)
{
    for (const auto& entry : fs::directory_iterator(COMPRESSOR_SAMPLES_DIR))
    {
        const std::string input = read_file(entry.path());
        EXPECT_EQ(inflate_with_zlib(deflate(input), -15), input) << entry.path().string();
    }
}

TEST(DeflateTests, A_gzip_member_carries_the_name_time_crc_and_size)
{
    const std::string input = text(10'000);
    std::istringstream in(input);
    std::ostringstream out;
    const auto result =
        compressor::write_gzip(in, out, {.name = "notes.txt", .mtime = 1'700'000'000});
    ASSERT_TRUE(result);

    gz_header header{};
    std::array<Bytef, 64> name{};
    header.name = name.data();
    header.name_max = name.size();
    EXPECT_EQ(inflate_with_zlib(out.str(), 31, &header), input);
    EXPECT_STREQ(reinterpret_cast<const char*>(name.data()), "notes.txt");
    EXPECT_EQ(header.time, 1'700'000'000U);
    EXPECT_EQ(header.os, 3);
}
