#ifndef Crc32_h
#define Crc32_h

#include <cstdint>
#include <span>

namespace compressor
{

// CRC-32/ISO-HDLC, as in gzip, zip and zlib's crc32(). Pass the previous result as `crc` to
// continue over more bytes. Uses the hardware path when crc32_hardware() is true.
uint32_t crc32(std::span<const uint8_t> bytes, uint32_t crc = 0);

// The table-driven fallback, kept callable so tests can compare it with the hardware path.
uint32_t crc32_portable(std::span<const uint8_t> bytes, uint32_t crc = 0);

// Whether this CPU runs crc32()'s hardware path: ARMv8 CRC32 instructions, or x86 carry-less
// multiplication (PCLMULQDQ and SSE4.1). Checked once, on the first call.
bool crc32_hardware();

} // namespace compressor

#endif /* Crc32_h */
