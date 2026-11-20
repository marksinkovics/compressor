#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <ostream>
#include <sstream>
#include <string>

#include <compressor/data/data.h>
#include <compressor/deflate/inflate.h>
#include <compressor/engine/engine.h>
#include <compressor/stream/decoder.h>

#include "dictionary.h"
#include "discard.h"

// NOLINTNEXTLINE(readability-identifier-naming): libFuzzer's entry point
extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, std::size_t size)
{
    static compressor::BlockDecoder decoder([] {
        return std::make_unique<compressor::Engine>(compressor::default_block_size_log2, 1,
                                                    fuzz::dictionary());
    }, 2);
    const std::string bytes(reinterpret_cast<const char*>(data), size);
    fuzz::DiscardBuf discard;
    std::ostream out(&discard);
    std::istringstream plain_in(bytes);
    const bool decoded = decoder.decode_frame(plain_in, out, 0).has_value();
    std::istringstream archive_in(bytes);
    const bool archive_decoded =
        decoder.decode_frame(archive_in, out, compressor::archive_flag).has_value();
    std::istringstream in(bytes);
    const bool parsed = compressor::read_encoded(in).has_value();
    std::istringstream gzip_in(bytes);
    (void)compressor::gunzip(gzip_in, out).has_value();
    // A container that decodes also parses, and nothing is both a plain container and an archive.
    if ((decoded && !parsed) || (decoded && archive_decoded))
    {
        std::abort();
    }
    return 0;
}
