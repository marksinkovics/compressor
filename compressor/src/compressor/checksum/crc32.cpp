#include <compressor/checksum/crc32.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace compressor
{

namespace
{

using Tables = std::array<std::array<uint32_t, 256>, 8>;

// tables[k][b] is the CRC of byte b followed by k zero bytes, for slicing-by-8.
constexpr Tables make_tables()
{
    constexpr uint32_t polynomial = 0xEDB88320U; // reflected 0x04C11DB7
    Tables tables{};
    for (uint32_t byte = 0; byte < 256; ++byte)
    {
        uint32_t crc = byte;
        for (int bit = 0; bit < 8; ++bit)
        {
            crc = (crc >> 1U) ^ ((crc & 1U) != 0 ? polynomial : 0U);
        }
        tables[0][byte] = crc;
    }
    for (std::size_t k = 1; k < tables.size(); ++k)
    {
        for (std::size_t byte = 0; byte < 256; ++byte)
        {
            const uint32_t previous = tables[k - 1][byte];
            tables[k][byte] = (previous >> 8U) ^ tables[0][previous & 0xFFU];
        }
    }
    return tables;
}

constexpr Tables tables = make_tables();

uint32_t load_le32(std::span<const uint8_t, 4> bytes)
{
    return static_cast<uint32_t>(bytes[0]) | (static_cast<uint32_t>(bytes[1]) << 8U) |
           (static_cast<uint32_t>(bytes[2]) << 16U) | (static_cast<uint32_t>(bytes[3]) << 24U);
}

} // namespace

uint32_t crc32(std::span<const uint8_t> bytes, uint32_t crc)
{
    crc = ~crc;
    while (bytes.size() >= 8)
    {
        const uint32_t low = crc ^ load_le32(bytes.first<4>());
        const uint32_t high = load_le32(bytes.subspan<4, 4>());
        crc = tables[7][low & 0xFFU] ^ tables[6][(low >> 8U) & 0xFFU] ^
              tables[5][(low >> 16U) & 0xFFU] ^ tables[4][low >> 24U] ^ tables[3][high & 0xFFU] ^
              tables[2][(high >> 8U) & 0xFFU] ^ tables[1][(high >> 16U) & 0xFFU] ^
              tables[0][high >> 24U];
        bytes = bytes.subspan(8);
    }
    for (const uint8_t byte : bytes)
    {
        crc = (crc >> 8U) ^ tables[0][(crc ^ byte) & 0xFFU];
    }
    return ~crc;
}

} // namespace compressor
