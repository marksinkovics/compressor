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

#include <compressor/entropy/rans.h>
#include <compressor/entropy/tans.h>
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

} // namespace

void register_entropy_benchmarks()
{
    struct Input
    {
        const char* name;
        unsigned stride;
    };
    const fs::path silesia = fs::path(COMPRESSOR_CORPUS_DIR) / "silesia";
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
