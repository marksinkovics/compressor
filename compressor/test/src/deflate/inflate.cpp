#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <string>
#include <vector>

#include <gtest/gtest.h>
#include <zlib.h>

#include <compressor/deflate/deflate.h>
#include <compressor/deflate/inflate.h>
#include <compressor/error.h>

namespace
{

using Bytes = std::vector<uint8_t>;

Bytes bytes_of(const std::string& text)
{
    return {text.begin(), text.end()};
}

Bytes text(std::size_t size)
{
    std::string out;
    uint32_t state = 5;
    while (out.size() < size)
    {
        state = (state * 1103515245U) + 12345U;
        out += "the quick brown fox jumps over the lazy dog "[(state >> 16U) % 44];
        if ((state >> 20U) % 7 == 0)
        {
            out += "the lazy dog ";
        }
    }
    out.resize(size);
    return bytes_of(out);
}

Bytes noise(std::size_t size)
{
    Bytes bytes(size);
    uint32_t state = 3;
    for (uint8_t& byte : bytes)
    {
        state = (state * 1103515245U) + 12345U;
        byte = static_cast<uint8_t>(state >> 16U);
    }
    return bytes;
}

// zlib's raw Deflate with the given level and strategy.
Bytes zlib_deflate(const Bytes& input, int level, int strategy)
{
    z_stream stream{};
    EXPECT_EQ(deflateInit2(&stream, level, Z_DEFLATED, -15, 8, strategy), Z_OK);
    Bytes out(deflateBound(&stream, static_cast<uLong>(input.size())) + 16);
    stream.next_in = const_cast<Bytef*>(input.data());
    stream.avail_in = static_cast<uInt>(input.size());
    stream.next_out = out.data();
    stream.avail_out = static_cast<uInt>(out.size());
    EXPECT_EQ(deflate(&stream, Z_FINISH), Z_STREAM_END);
    out.resize(stream.total_out);
    deflateEnd(&stream);
    return out;
}

std::expected<Bytes, compressor::Error> inflate(const Bytes& compressed, std::size_t size)
{
    Bytes out{0xAA}; // inflate appends
    if (auto inflated = compressor::inflate_bytes(compressed, out, size); !inflated)
    {
        EXPECT_EQ(out.size(), 1U + std::min(out.size() - 1, size)) << "wrote past the size";
        return std::unexpected(inflated.error());
    }
    return Bytes(out.begin() + 1, out.end());
}

} // namespace

TEST(InflateTests, Our_streams_inflate_back)
{
    for (const Bytes& input : {Bytes{}, bytes_of("a"), text(100'000), noise(70'000)})
    {
        EXPECT_EQ(inflate(compressor::deflate_bytes(input), input.size()), input) << input.size();
    }
}

TEST(InflateTests, Zlib_streams_of_every_kind_inflate)
{
    const Bytes input = [] {
        Bytes all = text(150'000);
        const Bytes random = noise(20'000);
        all.insert(all.end(), random.begin(), random.end());
        all.insert(all.end(), 3000, 0);
        return all;
    }();
    for (const int strategy : {Z_DEFAULT_STRATEGY, Z_FILTERED, Z_HUFFMAN_ONLY, Z_RLE, Z_FIXED})
    {
        for (const int level : {0, 1, 6, 9})
        {
            EXPECT_EQ(inflate(zlib_deflate(input, level, strategy), input.size()), input)
                << "strategy " << strategy << " level " << level;
        }
    }
}

TEST(InflateTests, Every_truncation_fails_cleanly)
{
    const Bytes input = text(3000);
    const Bytes compressed = compressor::deflate_bytes(input);
    for (std::size_t length = 0; length < compressed.size(); ++length)
    {
        const Bytes part(compressed.begin(),
                         compressed.begin() + static_cast<std::ptrdiff_t>(length));
        EXPECT_FALSE(inflate(part, input.size())) << length;
    }
}

TEST(InflateTests, The_size_must_match_and_nothing_may_follow)
{
    const Bytes input = text(5000);
    Bytes compressed = compressor::deflate_bytes(input);
    EXPECT_EQ(inflate(compressed, input.size() - 1),
              std::unexpected(compressor::Error::corrupt_payload));
    EXPECT_EQ(inflate(compressed, input.size() + 1),
              std::unexpected(compressor::Error::corrupt_payload));
    compressed.push_back(0);
    EXPECT_EQ(inflate(compressed, input.size()),
              std::unexpected(compressor::Error::corrupt_payload));
}

TEST(InflateTests, Malformed_blocks_are_rejected)
{
    // Final block of the reserved type 3.
    EXPECT_FALSE(inflate({0x07}, 0));
    // Stored block whose length and its complement disagree.
    EXPECT_FALSE(inflate({0x01, 0x05, 0x00, 0x00, 0x00, 'a', 'b', 'c', 'd', 'e'}, 5));
    // Fixed block: a match at distance 1 with nothing before it ("a" first would be fine).
    // Bits: final=1, type=01, then length code 257 (0000001, 7 bits), distance code 0 (00000).
    EXPECT_FALSE(inflate({0x03, 0x02, 0x00}, 3));
}

TEST(InflateTests, Random_bytes_never_crash)
{
    for (uint32_t seed = 0; seed < 2000; ++seed)
    {
        Bytes garbage(1 + (seed % 64));
        uint32_t state = seed;
        for (uint8_t& byte : garbage)
        {
            state = (state * 1103515245U) + 12345U;
            byte = static_cast<uint8_t>(state >> 16U);
        }
        const auto result = inflate(garbage, seed % 300);
        EXPECT_TRUE(result || result.error() == compressor::Error::corrupt_payload) << seed;
    }
}
