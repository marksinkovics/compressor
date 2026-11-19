#ifndef Error_h
#define Error_h

#include <cstdint>
#include <string_view>

namespace compressor
{

enum class Error : std::uint8_t
{
    read_failed,
    write_failed,
    not_ingot,
    unsupported_version,
    truncated,
    corrupt_header,
    corrupt_payload,
    checksum_mismatch,
};

std::string_view describe(Error error);

} // namespace compressor

#endif /* Error_h */
