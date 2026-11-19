#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>
#include <zlib.h>

#include <compressor/checksum/crc32.h>

namespace
{

std::span<const uint8_t> bytes_of(std::string_view text)
{
    return {reinterpret_cast<const uint8_t*>(text.data()), text.size()};
}

std::vector<uint8_t> pseudo_random(std::size_t size)
{
    std::vector<uint8_t> bytes(size);
    uint32_t state = 2463534242U;
    for (uint8_t& byte : bytes)
    {
        state ^= state << 13U;
        state ^= state >> 17U;
        state ^= state << 5U;
        byte = static_cast<uint8_t>(state);
    }
    return bytes;
}

uint32_t zlib_crc32(std::span<const uint8_t> bytes)
{
    return static_cast<uint32_t>(::crc32(0, bytes.data(), static_cast<uInt>(bytes.size())));
}

} // namespace

TEST(Crc32Tests, Matches_the_standard_check_value)
{
    EXPECT_EQ(compressor::crc32(bytes_of("123456789")), 0xCBF43926U);
}

TEST(Crc32Tests, Empty_input_is_zero)
{
    EXPECT_EQ(compressor::crc32({}), 0U);
}

TEST(Crc32Tests, Matches_zlib_for_every_length_up_to_64_and_large_input)
{
    const std::vector<uint8_t> bytes = pseudo_random(1'000'003);
    for (std::size_t length = 0; length <= 64; ++length)
    {
        const auto prefix = std::span(bytes).first(length);
        EXPECT_EQ(compressor::crc32(prefix), zlib_crc32(prefix)) << length;
    }
    EXPECT_EQ(compressor::crc32(bytes), zlib_crc32(bytes));
}

TEST(Crc32Tests, Continues_across_calls)
{
    const std::vector<uint8_t> bytes = pseudo_random(10'000);
    const auto all = std::span<const uint8_t>(bytes);
    for (const std::size_t split : {0UZ, 1UZ, 7UZ, 8UZ, 4999UZ, 10'000UZ})
    {
        const uint32_t first = compressor::crc32(all.first(split));
        EXPECT_EQ(compressor::crc32(all.subspan(split), first), compressor::crc32(all)) << split;
    }
}
