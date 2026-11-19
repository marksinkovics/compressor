#include <compressor/error.h>

#include <string_view>

namespace compressor
{

std::string_view describe(Error error)
{
    switch (error)
    {
    case Error::read_failed:
        return "the input could not be read";
    case Error::not_ingot:
        return "it is not an ingot file";
    case Error::unsupported_version:
        return "its format version is not supported";
    case Error::truncated:
        return "the file ends too early";
    case Error::corrupt_header:
        return "a header is corrupt";
    case Error::corrupt_payload:
        return "a payload is corrupt";
    case Error::checksum_mismatch:
        return "a block's checksum does not match its data";
    }
    return "unknown error";
}

} // namespace compressor
