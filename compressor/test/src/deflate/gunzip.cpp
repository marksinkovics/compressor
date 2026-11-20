#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <istream>
#include <sstream>
#include <streambuf>
#include <string>
#include <vector>

#include <gtest/gtest.h>
#include <zlib.h>

#include <compressor/deflate/deflate.h>
#include <compressor/deflate/inflate.h>
#include <compressor/error.h>

namespace
{

std::string text(std::size_t size, uint32_t seed = 1)
{
    std::string out;
    while (out.size() < size)
    {
        seed = (seed * 1103515245U) + 12345U;
        out += "gzip member stream window "[(seed >> 16U) % 26];
        if ((seed >> 20U) % 5 == 0)
        {
            out += "a repeated phrase ";
        }
    }
    out.resize(size);
    return out;
}

// zlib's gzip, with every optional header field when `full_header`.
std::string zlib_gzip(const std::string& input, int level, bool full_header)
{
    z_stream stream{};
    EXPECT_EQ(deflateInit2(&stream, level, Z_DEFLATED, 31, 8, Z_DEFAULT_STRATEGY), Z_OK);
    std::array<Bytef, 5> extra{'x', 'y', 2, 0, 'z'};
    std::string name = "file.txt";
    std::string comment = "a comment";
    gz_header header{};
    if (full_header)
    {
        header.extra = extra.data();
        header.extra_len = static_cast<uInt>(extra.size());
        header.name = reinterpret_cast<Bytef*>(name.data());
        header.comment = reinterpret_cast<Bytef*>(comment.data());
        header.hcrc = 1;
        EXPECT_EQ(deflateSetHeader(&stream, &header), Z_OK);
    }
    std::string out(deflateBound(&stream, static_cast<uLong>(input.size())) + 64, '\0');
    stream.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(input.data()));
    stream.avail_in = static_cast<uInt>(input.size());
    stream.next_out = reinterpret_cast<Bytef*>(out.data());
    stream.avail_out = static_cast<uInt>(out.size());
    EXPECT_EQ(deflate(&stream, Z_FINISH), Z_STREAM_END);
    out.resize(stream.total_out);
    deflateEnd(&stream);
    return out;
}

// Hands out one byte per underflow, so every refill boundary is crossed.
class TrickleBuffer : public std::streambuf
{
public:
    explicit TrickleBuffer(std::string bytes)
        : bytes_(std::move(bytes))
    {
    }

protected:
    int_type underflow() override
    {
        if (next_ >= bytes_.size())
        {
            return traits_type::eof();
        }
        char* at = bytes_.data() + next_;
        setg(at, at, at + 1);
        ++next_;
        return traits_type::to_int_type(*at);
    }

private:
    std::string bytes_;
    std::size_t next_ = 0;
};

std::expected<std::string, compressor::Error> gunzip(const std::string& file, bool trickle = false)
{
    TrickleBuffer slow(file);
    std::istringstream fast(file);
    std::istream& in = trickle ? static_cast<std::istream&>(*new std::istream(&slow)) : fast;
    std::ostringstream out;
    const auto result = compressor::gunzip(in, out);
    if (trickle)
    {
        delete &in;
    }
    if (!result)
    {
        return std::unexpected(result.error());
    }
    EXPECT_EQ(*result, out.str().size());
    return out.str();
}

} // namespace

TEST(GunzipTests, Zlib_files_decode_with_and_without_optional_header_fields)
{
    for (const int level : {0, 1, 6, 9})
    {
        const std::string input = text(300'000, static_cast<uint32_t>(level));
        EXPECT_EQ(gunzip(zlib_gzip(input, level, false)), input) << level;
        EXPECT_EQ(gunzip(zlib_gzip(input, level, true)), input) << level;
    }
}

TEST(GunzipTests, Our_files_decode_and_members_follow_one_another)
{
    const std::string first = text(3'000'000, 7); // beyond the 1 MiB the window flushes at
    const std::string second = text(1000, 8);
    std::istringstream in(first);
    std::ostringstream ours;
    ASSERT_TRUE(compressor::write_gzip(in, ours, {.name = "a", .mtime = 1}));
    const std::string both = ours.str() + zlib_gzip(second, 6, true);
    EXPECT_EQ(gunzip(both), first + second);
    EXPECT_EQ(gunzip(zlib_gzip("", 6, false)), "");
}

TEST(GunzipTests, A_trickling_stream_decodes_the_same)
{
    const std::string input = text(100'000, 3);
    const std::string file = zlib_gzip(input, 6, true) + zlib_gzip("tail", 9, false);
    EXPECT_EQ(gunzip(file, true), input + "tail");
}

TEST(GunzipTests, Damaged_files_are_rejected)
{
    using compressor::Error;
    const std::string input = text(50'000, 4);
    const std::string file = zlib_gzip(input, 6, true);

    EXPECT_EQ(gunzip("not gzip at all"), std::unexpected(Error::not_ingot));
    EXPECT_EQ(gunzip(file.substr(0, 5)), std::unexpected(Error::truncated));
    EXPECT_FALSE(gunzip(file.substr(0, file.size() / 2)));
    EXPECT_EQ(gunzip(file.substr(0, file.size() - 3)), std::unexpected(Error::truncated));

    std::string bad_crc = file;
    bad_crc[bad_crc.size() - 8] =
        static_cast<char>(static_cast<uint8_t>(bad_crc[bad_crc.size() - 8]) ^ 1U);
    EXPECT_EQ(gunzip(bad_crc), std::unexpected(Error::checksum_mismatch));
    std::string bad_size = file;
    bad_size[bad_size.size() - 4] =
        static_cast<char>(static_cast<uint8_t>(bad_size[bad_size.size() - 4]) ^ 1U);
    EXPECT_EQ(gunzip(bad_size), std::unexpected(Error::corrupt_payload));
    std::string bad_header_crc = file;
    bad_header_crc[12] =
        static_cast<char>(static_cast<uint8_t>(bad_header_crc[12]) ^ 1U); // in the extra field
    EXPECT_EQ(gunzip(bad_header_crc), std::unexpected(Error::checksum_mismatch));
    std::string reserved_flag = file;
    reserved_flag[3] = static_cast<char>(static_cast<uint8_t>(reserved_flag[3]) | 0x80U);
    EXPECT_EQ(gunzip(reserved_flag), std::unexpected(Error::corrupt_header));
    EXPECT_EQ(gunzip(file + "junk"), std::unexpected(Error::corrupt_header));
}
