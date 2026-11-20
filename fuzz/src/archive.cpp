#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <ostream>
#include <sstream>
#include <string>
#include <string_view>

#include <compressor/archive/archive.h>
#include <compressor/archive/index.h>
#include <compressor/engine/engine.h>
#include <compressor/stream/decoder.h>

#include "discard.h"

namespace
{

// The rules extraction relies on to stay inside its directory, checked independently of
// validate_index().
bool stays_inside(std::string_view path)
{
    if (path.empty() || path.front() == '/' || path.find('\0') != std::string_view::npos)
    {
        return false;
    }
    std::size_t start = 0;
    while (start <= path.size())
    {
        const std::size_t end = std::min(path.find('/', start), path.size());
        const std::string_view part = path.substr(start, end - start);
        if (part.empty() || part == "." || part == "..")
        {
            return false;
        }
        start = end + 1;
    }
    return true;
}

} // namespace

// NOLINTNEXTLINE(readability-identifier-naming): libFuzzer's entry point
extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, std::size_t size)
{
    static compressor::BlockDecoder decoder([] {
        return std::make_unique<compressor::Engine>();
    }, 2);
    const std::string bytes(reinterpret_cast<const char*>(data), size);
    std::istringstream in(bytes);
    const auto info = compressor::read_archive_index(in);
    if (!info)
    {
        return 0;
    }
    for (const compressor::Entry& entry : info->index.entries)
    {
        if (!stays_inside(entry.path))
        {
            std::abort();
        }
    }

    fuzz::DiscardBuf discard;
    std::ostream out(&discard);
    compressor::list_archive(*info, true, out);
    const bool verified = compressor::verify_archive(decoder, in, *info).has_value();
    for (const compressor::Entry& entry : info->index.entries)
    {
        if (entry.type == compressor::EntryType::file)
        {
            // What verifies whole also extracts file by file.
            const bool extracted =
                compressor::extract_file(decoder, in, *info, entry, out).has_value();
            if (verified && !extracted)
            {
                std::abort();
            }
        }
        else if (!compressor::select_subtree(info->index.entries, entry.path))
        {
            std::abort();
        }
    }
    return 0;
}
