#include "cm.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <span>
#include <string>
#include <vector>

#include <benchmark/benchmark.h>

#include <compressor/bwt/bwt.h>
#include <compressor/cm/cm.h>
#include <compressor/lz77/lz77.h>
#include <compressor/lzr/lzr.h>

namespace compressor::bench
{

namespace
{

namespace fs = std::filesystem;

std::vector<uint8_t> read_file(const fs::path& path)
{
    std::ifstream file(path, std::ios::in | std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

// Per 1 MiB block, like the container: CM, BWT and LZR sizes, CM checked by decoding.
void cm_against_others(::benchmark::State& state, const fs::path& path)
{
    const std::vector<uint8_t> data = read_file(path);
    std::size_t cm = 0;
    for (auto _ : state)
    {
        cm = 0;
        for (std::size_t at = 0; at < data.size(); at += std::size_t{1} << 20U)
        {
            const std::span<const uint8_t> block(data.data() + at,
                                                 std::min(data.size() - at, std::size_t{1} << 20U));
            cm += cm_encode(block).size();
        }
    }
    std::size_t bwt = 0;
    std::size_t lzr = 0;
    for (std::size_t at = 0; at < data.size(); at += std::size_t{1} << 20U)
    {
        const std::span<const uint8_t> block(data.data() + at,
                                             std::min(data.size() - at, std::size_t{1} << 20U));
        const std::vector<uint8_t> coded = cm_encode(block);
        std::vector<uint8_t> decoded;
        if (!cm_decode(coded, decoded, block.size()) ||
            decoded != std::vector<uint8_t>(block.begin(), block.end()))
        {
            state.SkipWithError("CM did not decode to the input");
            return;
        }
        bwt += bwt_encode(block).size();
        lzr += lzr_encode(block, lz77_options_for_level(7)).size();
    }
    state.SetBytesProcessed(state.iterations() * static_cast<int64_t>(data.size()));
    state.counters["cm"] = static_cast<double>(cm);
    state.counters["bwt"] = static_cast<double>(bwt);
    state.counters["lzr"] = static_cast<double>(lzr);
}

} // namespace

void register_cm_benchmarks()
{
    const fs::path silesia = fs::path(COMPRESSOR_CORPUS_DIR) / "silesia";
    for (const char* name : {"dickens", "mozilla", "mr", "nci", "ooffice", "osdb", "reymont",
                             "samba", "sao", "webster", "x-ray", "xml"})
    {
        const fs::path path = silesia / name;
        if (!fs::exists(path))
        {
            std::cerr << "No " << path.string() << ", skipping the context mixing experiment\n";
            return;
        }
        ::benchmark::RegisterBenchmark(std::string("ContextMixing/") + name, cm_against_others,
                                       path)
            ->Unit(::benchmark::kMillisecond)
            ->Iterations(1);
    }
}

} // namespace compressor::bench
