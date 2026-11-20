#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <ostream>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include <compressor/archive/archive.h>
#include <compressor/archive/index.h>
#include <compressor/engine/engine.h>
#include <compressor/search/search.h>
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

class SearchVisitor : public compressor::FileVisitor
{
public:
    explicit SearchVisitor(compressor::LineSearcher& searcher)
        : searcher_(searcher)
    {
    }

    void begin(std::span<const std::filesystem::path> paths) override
    {
        searcher_.begin(std::vector<std::string>(paths.size(), "f"), true);
    }

    void data(std::string_view bytes) override
    {
        searcher_.feed(bytes);
    }

    void end() override
    {
        searcher_.end();
    }

private:
    compressor::LineSearcher& searcher_;
};

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
    compressor::LineSearcher searcher({.pattern = "a", .output = {}, .byte_offsets = true}, out);
    SearchVisitor visitor(searcher);
    if (verified && !compressor::visit_files(decoder, in, *info, info->index.entries, visitor))
    {
        std::abort();
    }
    return 0;
}
