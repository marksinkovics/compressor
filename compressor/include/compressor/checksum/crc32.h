#ifndef Crc32_h
#define Crc32_h

#include <cstdint>
#include <span>

namespace compressor
{

// CRC-32/ISO-HDLC, as in gzip, zip and zlib's crc32(). Pass the previous result as `crc` to
// continue over more bytes.
uint32_t crc32(std::span<const uint8_t> bytes, uint32_t crc = 0);

} // namespace compressor

#endif /* Crc32_h */
