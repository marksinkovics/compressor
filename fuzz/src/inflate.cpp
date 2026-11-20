#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <span>
#include <vector>

#include <zlib.h>

#include <compressor/deflate/inflate.h>

// Our inflater against zlib's on the same input: both must accept it with the same bytes, or both
// reject it. The first two bytes are the expected size.
// NOLINTNEXTLINE(readability-identifier-naming): libFuzzer's entry point
extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, std::size_t size)
{
    if (size < 2)
    {
        return 0;
    }
    const std::size_t expected = data[0] | (std::size_t{data[1]} << 8U);
    const std::span<const uint8_t> stream(data + 2, size - 2);

    std::vector<uint8_t> ours;
    const bool ours_ok = compressor::inflate_bytes(stream, ours, expected).has_value();

    // One spare byte, so zlib producing more than `expected` shows up.
    std::vector<uint8_t> theirs(expected + 1);
    z_stream zlib{};
    if (inflateInit2(&zlib, -15) != Z_OK)
    {
        std::abort();
    }
    zlib.next_in = const_cast<Bytef*>(stream.data());
    zlib.avail_in = static_cast<uInt>(stream.size());
    zlib.next_out = theirs.data();
    zlib.avail_out = static_cast<uInt>(theirs.size());
    const int status = inflate(&zlib, Z_FINISH);
    const bool theirs_ok =
        status == Z_STREAM_END && zlib.total_out == expected && zlib.avail_in == 0;
    theirs.resize(zlib.total_out);
    inflateEnd(&zlib);

    if (ours_ok != theirs_ok || (ours_ok && ours != theirs))
    {
        std::abort();
    }
    return 0;
}
