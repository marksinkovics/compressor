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
    case Error::write_failed:
        return "the output could not be written";
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
    case Error::unexpected_archive:
        return "it is a folder archive, which must be decoded from a file into a directory";
    case Error::corrupt_index:
        return "the archive index is corrupt";
    case Error::file_failed:
        return "a file could not be read or written";
    case Error::dictionary_missing:
        return "it was encoded with a dictionary; give it with -D";
    case Error::dictionary_mismatch:
        return "it was encoded with a different dictionary";
    case Error::bad_range:
        return "the range starts past the end of the data";
    }
    return "unknown error";
}

} // namespace compressor
