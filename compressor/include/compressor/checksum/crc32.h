#ifndef Crc32_h
#define Crc32_h

#include <cstdint>
#include <span>

namespace compressor
{

// CRC-32/ISO-HDLC, as in gzip, zip and zlib's crc32(). Pass the previous result as `crc` to
// continue over more bytes. Uses the hardware path when crc32_hardware is true.
uint32_t crc32(std::span<const uint8_t> bytes, uint32_t crc = 0);

// The table-driven fallback, kept callable so tests can compare it with the hardware path.
uint32_t crc32_portable(std::span<const uint8_t> bytes, uint32_t crc = 0);

// Chosen at compile time: ARMv8 CRC32 instructions (__ARM_FEATURE_CRC32), or x86 carry-less
// multiplication (__PCLMUL__ and __SSE4_1__, e.g. -mpclmul -msse4.1 or -march=x86-64-v3).
extern const bool crc32_hardware;

} // namespace compressor

#endif /* Crc32_h */
