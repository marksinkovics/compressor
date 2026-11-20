#include "entropy.h"

#include <algorithm>
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

#include <compressor/deflate/deflate.h>
#include <compressor/entropy/rans.h>
#include <compressor/entropy/tans.h>
#include <compressor/lz77/lz77.h>
#include <compressor/lzr/lzr.h>
#include <compressor/lzt/lzt.h>
#include <compressor/transform/transform.h>

namespace compressor::bench
{

namespace
{

namespace fs = std::filesystem;

constexpr std::size_t block = std::size_t{1} << 20U;

std::vector<uint8_t> read_file(const fs::path& path)
{
    std::ifstream file(path, std::ios::in | std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

enum class Coder : uint8_t
{
    rans,
    tans
};

std::vector<uint8_t> encode(Coder coder, std::span<const uint8_t> bytes)
{
    std::vector<uint8_t> out;
    if (coder == Coder::rans)
    {
        rans_encode(std::vector<uint16_t>(bytes.begin(), bytes.end()), 256, out);
    }
    else
    {
        tans_encode(bytes, out);
    }
    return out;
}

bool decode(Coder coder, std::span<const uint8_t> coded, std::size_t size,
            std::vector<uint8_t>& out)
{
    if (coder == Coder::rans)
    {
        auto symbols = rans_decode(coded, 256, size);
        if (!symbols)
        {
            return false;
        }
        out.assign(symbols->begin(), symbols->end());
        return true;
    }
    out.clear();
    return tans_decode(coded, size, out).has_value();
}

void order0(::benchmark::State& state, const fs::path& path, unsigned stride, Coder coder,
            bool decoding)
{
    std::vector<uint8_t> data = read_file(path);
    if (stride > 0)
    {
        apply_transform({.id = TransformId::delta, .parameter = stride}, data);
    }
    std::vector<std::vector<uint8_t>> coded;
    std::size_t size = 0;
    for (std::size_t at = 0; at < data.size(); at += block)
    {
        const std::span<const uint8_t> input(data.data() + at, std::min(data.size() - at, block));
        coded.push_back(encode(coder, input));
        size += coded.back().size();
        std::vector<uint8_t> decoded;
        if (!decode(coder, coded.back(), input.size(), decoded) ||
            !std::ranges::equal(decoded, input))
        {
            state.SkipWithError("did not decode to the input");
            return;
        }
    }
    std::vector<uint8_t> decoded;
    decoded.reserve(block);
    for (auto _ : state)
    {
        for (std::size_t i = 0; i < coded.size(); ++i)
        {
            const std::span<const uint8_t> input(data.data() + (i * block),
                                                 std::min(data.size() - (i * block), block));
            if (decoding)
            {
                ::benchmark::DoNotOptimize(decode(coder, coded[i], input.size(), decoded));
            }
            else
            {
                ::benchmark::DoNotOptimize(encode(coder, input));
            }
        }
    }
    state.SetBytesProcessed(state.iterations() * static_cast<int64_t>(data.size()));
    state.counters["size"] = static_cast<double>(size);
}

// Per 1 MiB block: LZT's size and decoding speed against LZ77 + Huffman (Deflate) and LZR.
void lzt_against_others(::benchmark::State& state, const fs::path& path, unsigned level)
{
    const std::vector<uint8_t> data = read_file(path);
    const Lz77Options options = lz77_options_for_level(level);
    std::vector<std::vector<uint8_t>> coded;
    std::size_t size = 0;
    std::size_t deflate = 0;
    std::size_t lzr = 0;
    for (std::size_t at = 0; at < data.size(); at += block)
    {
        const std::span<const uint8_t> input(data.data() + at, std::min(data.size() - at, block));
        coded.push_back(lzt_encode(input, options));
        size += coded.back().size();
        deflate += deflate_bytes(input, options).size();
        lzr += lzr_encode(input, lz77_options_for_level(7)).size();
        std::vector<uint8_t> decoded;
        if (!lzt_decode(coded.back(), decoded, input.size()) || !std::ranges::equal(decoded, input))
        {
            state.SkipWithError("LZT did not decode to the input");
            return;
        }
    }
    std::vector<uint8_t> decoded;
    for (auto _ : state)
    {
        for (std::size_t i = 0; i < coded.size(); ++i)
        {
            decoded.clear();
            ::benchmark::DoNotOptimize(
                lzt_decode(coded[i], decoded, std::min(data.size() - (i * block), block)));
        }
    }
    state.SetBytesProcessed(state.iterations() * static_cast<int64_t>(data.size()));
    state.counters["lzt"] = static_cast<double>(size);
    state.counters["deflate"] = static_cast<double>(deflate);
    state.counters["lzr"] = static_cast<double>(lzr);
}

} // namespace

void register_entropy_benchmarks()
{
    struct Input
    {
        const char* name;
        unsigned stride;
    };
    const fs::path silesia = fs::path(COMPRESSOR_CORPUS_DIR) / "silesia";
    for (const char* name : {"dickens", "mozilla", "mr", "nci", "ooffice", "osdb", "reymont",
                             "samba", "sao", "webster", "x-ray", "xml"})
    {
        if (fs::exists(silesia / name))
        {
            ::benchmark::RegisterBenchmark(std::string("Lzt/") + name, lzt_against_others,
                                           silesia / name, 6U)
                ->Unit(::benchmark::kMillisecond)
                ->Iterations(3);
        }
    }
    for (const Input input :
         {Input{.name = "dickens", .stride = 0}, Input{.name = "x-ray", .stride = 2},
          Input{.name = "mr", .stride = 1}, Input{.name = "sao", .stride = 4},
          Input{.name = "ooffice", .stride = 0}})
    {
        const fs::path path = silesia / input.name;
        if (!fs::exists(path))
        {
            std::cerr << "No " << path.string() << ", skipping the entropy coder benchmarks\n";
            return;
        }
        for (const Coder coder : {Coder::rans, Coder::tans})
        {
            for (const bool decoding : {false, true})
            {
                const std::string name = std::string("Order0/") +
                                         (coder == Coder::rans ? "rans/" : "tans/") +
                                         (decoding ? "decode/" : "encode/") + input.name +
                                         (input.stride > 0 ? "+delta" : "");
                ::benchmark::RegisterBenchmark(name, order0, path, input.stride, coder, decoding)
                    ->Unit(::benchmark::kMillisecond);
            }
        }
    }
}

} // namespace compressor::bench
