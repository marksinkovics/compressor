#include <cstddef>
#include <cstdint>
#include <ostream>
#include <sstream>
#include <string>

#include <compressor/stats/stats.h>

#include "discard.h"

// NOLINTNEXTLINE(readability-identifier-naming): libFuzzer's entry point
extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, std::size_t size)
{
    std::istringstream in(std::string(reinterpret_cast<const char*>(data), size));
    const auto stats = compressor::collect_stats(in);
    if (stats)
    {
        fuzz::DiscardBuf discard;
        std::ostream out(&discard);
        compressor::print_stats(*stats, {.blocks = true, .histogram = true}, out);
        compressor::write_content_graph(*stats, out);
    }
    return 0;
}
