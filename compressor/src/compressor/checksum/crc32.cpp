#include <compressor/checksum/crc32.h>

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>

// Each hardware path is compiled with a target attribute, so the build needs no -m flags, and is
// used only after a CPU check; when the compiler already targets the feature, the check is skipped.
#if defined(__aarch64__) && (defined(__GNUC__) || defined(__clang__))
#include <arm_acle.h>
#define COMPRESSOR_CRC32_ARM 1
#if defined(__ARM_FEATURE_CRC32)
#define COMPRESSOR_CRC32_TARGET
#elif defined(__clang__)
#define COMPRESSOR_CRC32_TARGET __attribute__((target("crc")))
#else
#define COMPRESSOR_CRC32_TARGET __attribute__((target("+crc")))
#endif
#if defined(__linux__)
#include <asm/hwcap.h>
#include <sys/auxv.h>
#elif defined(__APPLE__)
#include <sys/sysctl.h>
#endif
#elif defined(__x86_64__) && (defined(__GNUC__) || defined(__clang__))
#include <immintrin.h>
#define COMPRESSOR_CRC32_PCLMUL 1
#define COMPRESSOR_CRC32_TARGET __attribute__((target("pclmul,sse4.1")))
#elif defined(_M_X64) && defined(_MSC_VER)
#include <immintrin.h>
#include <intrin.h>
#define COMPRESSOR_CRC32_PCLMUL 1
#define COMPRESSOR_CRC32_TARGET
#endif

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

// Works on the inverted CRC, like the hardware paths, so they can hand their tail over.
uint32_t crc32_inverted_portable(std::span<const uint8_t> bytes, uint32_t crc)
{
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
    return crc;
}

#if defined(COMPRESSOR_CRC32_ARM)

bool cpu_has_crc32()
{
#if defined(__ARM_FEATURE_CRC32)
    return true;
#elif defined(__linux__)
    return (getauxval(AT_HWCAP) & HWCAP_CRC32) != 0;
#elif defined(__APPLE__)
    int value = 0;
    std::size_t size = sizeof(value);
    return sysctlbyname("hw.optional.armv8_crc32", &value, &size, nullptr, 0) == 0 && value != 0;
#else
    return false;
#endif
}

COMPRESSOR_CRC32_TARGET uint32_t crc32_inverted_hardware(std::span<const uint8_t> bytes,
                                                         uint32_t crc)
{
    static_assert(std::endian::native == std::endian::little);
    while (bytes.size() >= 8)
    {
        uint64_t word = 0;
        std::memcpy(&word, bytes.data(), sizeof(word));
        crc = __crc32d(crc, word);
        bytes = bytes.subspan(8);
    }
    for (const uint8_t byte : bytes)
    {
        crc = __crc32b(crc, byte);
    }
    return crc;
}

#elif defined(COMPRESSOR_CRC32_PCLMUL)

bool cpu_has_crc32()
{
#if defined(__PCLMUL__) && defined(__SSE4_1__)
    return true;
#elif defined(_MSC_VER)
    std::array<int, 4> registers{};
    __cpuid(registers.data(), 1);
    constexpr int pclmulqdq = 1 << 1;
    constexpr int sse4_1 = 1 << 19;
    return (registers[2] & pclmulqdq) != 0 && (registers[2] & sse4_1) != 0;
#else
    return __builtin_cpu_supports("pclmul") && __builtin_cpu_supports("sse4.1");
#endif
}

COMPRESSOR_CRC32_TARGET __m128i load128(std::span<const uint8_t> from)
{
    return _mm_loadu_si128(reinterpret_cast<const __m128i*>(from.data()));
}

COMPRESSOR_CRC32_TARGET __m128i fold(__m128i value, __m128i constants, __m128i next)
{
    const __m128i low = _mm_clmulepi64_si128(value, constants, 0x00);
    const __m128i high = _mm_clmulepi64_si128(value, constants, 0x11);
    return _mm_xor_si128(_mm_xor_si128(low, high), next);
}

// Folding with carry-less multiplication and a Barrett reduction, after Intel's "Fast CRC
// Computation for Generic Polynomials Using PCLMULQDQ Instruction" (the constants are
// x^n mod P for the reflected polynomial, as in zlib's and Chromium's versions).
COMPRESSOR_CRC32_TARGET uint32_t crc32_inverted_hardware(std::span<const uint8_t> bytes,
                                                         uint32_t crc)
{
    if (bytes.size() < 64)
    {
        return crc32_inverted_portable(bytes, crc);
    }
    __m128i x1 = _mm_xor_si128(load128(bytes), _mm_cvtsi32_si128(static_cast<int>(crc)));
    __m128i x2 = load128(bytes.subspan(16));
    __m128i x3 = load128(bytes.subspan(32));
    __m128i x4 = load128(bytes.subspan(48));
    bytes = bytes.subspan(64);

    const __m128i k1k2 = _mm_set_epi64x(0x01c6e41596, 0x0154442bd4);
    while (bytes.size() >= 64)
    {
        x1 = fold(x1, k1k2, load128(bytes));
        x2 = fold(x2, k1k2, load128(bytes.subspan(16)));
        x3 = fold(x3, k1k2, load128(bytes.subspan(32)));
        x4 = fold(x4, k1k2, load128(bytes.subspan(48)));
        bytes = bytes.subspan(64);
    }

    const __m128i k3k4 = _mm_set_epi64x(0x00ccaa009e, 0x01751997d0);
    x1 = fold(x1, k3k4, x2);
    x1 = fold(x1, k3k4, x3);
    x1 = fold(x1, k3k4, x4);
    while (bytes.size() >= 16)
    {
        x1 = fold(x1, k3k4, load128(bytes));
        bytes = bytes.subspan(16);
    }

    // 128 bits to 64.
    const __m128i low32 = _mm_setr_epi32(-1, 0, -1, 0);
    x1 = _mm_xor_si128(_mm_srli_si128(x1, 8), _mm_clmulepi64_si128(x1, k3k4, 0x10));
    const __m128i k5 = _mm_set_epi64x(0, 0x0163cd6124);
    x1 = _mm_xor_si128(_mm_clmulepi64_si128(_mm_and_si128(x1, low32), k5, 0x00),
                       _mm_srli_si128(x1, 4));

    // Barrett reduction to 32 bits.
    const __m128i poly = _mm_set_epi64x(0x01f7011641, 0x01db710641);
    __m128i x = _mm_clmulepi64_si128(_mm_and_si128(x1, low32), poly, 0x10);
    x = _mm_clmulepi64_si128(_mm_and_si128(x, low32), poly, 0x00);
    crc = static_cast<uint32_t>(_mm_extract_epi32(_mm_xor_si128(x1, x), 1));

    return crc32_inverted_portable(bytes, crc);
}

#else

bool cpu_has_crc32()
{
    return false;
}

uint32_t crc32_inverted_hardware(std::span<const uint8_t> bytes, uint32_t crc)
{
    return crc32_inverted_portable(bytes, crc);
}

#endif

} // namespace

bool crc32_hardware()
{
    static const bool available = cpu_has_crc32();
    return available;
}

uint32_t crc32(std::span<const uint8_t> bytes, uint32_t crc)
{
    return ~(crc32_hardware() ? crc32_inverted_hardware(bytes, ~crc)
                              : crc32_inverted_portable(bytes, ~crc));
}

uint32_t crc32_portable(std::span<const uint8_t> bytes, uint32_t crc)
{
    return ~crc32_inverted_portable(bytes, ~crc);
}

} // namespace compressor
