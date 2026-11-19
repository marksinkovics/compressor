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
    case Error::corrupt_header:
        return "the header is corrupt";
    case Error::corrupt_payload:
        return "the payload is corrupt";
    }
    return "unknown error";
}

} // namespace compressor
