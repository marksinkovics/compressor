#include "search.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <benchmark/benchmark.h>

#include <compressor/data/data.h>
#include <compressor/engine/engine.h>
#include <compressor/huffman/huffman.h>

// Search without decompressing (roadmap, autonomous run item 6): is scanning a Huffman payload
// for the encoded pattern faster than decoding and searching the text? BitScan only finds
// candidates, at every bit offset; telling real matches (which start on a code boundary) from the
// rest would cost more on top. So if BitScan alone loses, the idea loses.

namespace compressor::bench
{

namespace
{

namespace fs = std::filesystem;

struct Prepared
{
    EncodedData encoded;
    std::size_t size = 0;
};

const Prepared& prepare(const fs::path& path)
{
    static fs::path current;
    static Prepared prepared;
    if (current != path)
    {
        std::ifstream file(path, std::ios::in | std::ios::binary);
        DecodedData input;
        input.data_.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
        Engine engine;
        prepared = {.encoded = engine.encode(input), .size = input.data_.size()};
        current = path;
    }
    return prepared;
}

std::size_t count_matches(std::string_view text, std::string_view pattern)
{
    std::size_t count = 0;
    for (std::size_t at = text.find(pattern); at != std::string_view::npos;
         at = text.find(pattern, at + 1))
    {
        ++count;
    }
    return count;
}

void decode_and_find(::benchmark::State& state, const fs::path& path, const std::string& pattern)
{
    const Prepared& file = prepare(path);
    Engine engine;
    std::vector<uint8_t> bytes;
    std::size_t matches = 0;
    for (auto _ : state)
    {
        matches = 0;
        for (const EncodedBlock& block : file.encoded.blocks_)
        {
            bytes.clear();
            if (!engine.decode_block(block, bytes))
            {
                state.SkipWithError("decode failed");
                return;
            }
            matches +=
                count_matches({reinterpret_cast<const char*>(bytes.data()), bytes.size()}, pattern);
        }
        ::benchmark::DoNotOptimize(matches);
    }
    state.SetBytesProcessed(state.iterations() * static_cast<int64_t>(file.size));
    state.counters["matches"] = static_cast<double>(matches);
}

// The pattern's codes, first bit most significant, in the low `length` bits. A longer pattern is
// cut to the codes that fit in 56 bits, which still filters candidates.
std::optional<std::pair<uint64_t, unsigned>> encode_pattern(const Codes& codes,
                                                            std::string_view pattern)
{
    uint64_t bits = 0;
    unsigned length = 0;
    for (const char c : pattern)
    {
        const Code code = codes[static_cast<uint8_t>(c)];
        if (code.length == 0)
        {
            return std::nullopt; // a symbol the block doesn't have: no match possible
        }
        if (length + code.length > 56)
        {
            break;
        }
        length += code.length;
        bits = (bits << code.length) | code.bits;
    }
    return std::pair{bits, length};
}

std::size_t bit_candidates(std::span<const uint8_t> payload, uint64_t pattern, unsigned length)
{
    std::size_t candidates = 0;
    uint64_t window = 0; // the next 64 bits, first bit most significant
    const std::size_t bytes = payload.size();
    for (std::size_t i = 0; i < 8; ++i)
    {
        window = (window << 8U) | (i < bytes ? payload[i] : 0U);
    }
    const unsigned shift = 64 - length;
    for (std::size_t i = 0; i < bytes; ++i)
    {
        for (unsigned bit = 0; bit < 8; ++bit)
        {
            candidates += static_cast<std::size_t>(((window << bit) >> shift) == pattern);
        }
        window = (window << 8U) | (i + 8 < bytes ? payload[i + 8] : 0U);
    }
    return candidates;
}

void bit_scan(::benchmark::State& state, const fs::path& path, const std::string& pattern)
{
    const Prepared& file = prepare(path);
    std::size_t candidates = 0;
    for (auto _ : state)
    {
        candidates = 0;
        for (const EncodedBlock& block : file.encoded.blocks_)
        {
            if (block.method_ == Method::stored)
            {
                candidates += count_matches(
                    {reinterpret_cast<const char*>(block.payload_.data()), block.payload_.size()},
                    pattern);
                continue;
            }
            // Building the codes is part of the cost, as a search has to do it per block.
            const auto encoded = encode_pattern(canonical_codes(block.code_lengths_), pattern);
            if (encoded)
            {
                candidates += bit_candidates(block.payload_, encoded->first, encoded->second);
            }
        }
        ::benchmark::DoNotOptimize(candidates);
    }
    state.SetBytesProcessed(state.iterations() * static_cast<int64_t>(file.size));
    state.counters["candidates"] = static_cast<double>(candidates);
}

} // namespace

void register_search_benchmarks()
{
    const fs::path silesia = fs::path(COMPRESSOR_CORPUS_DIR) / "silesia";
    const std::array<std::pair<const char*, const char*>, 4> cases{{
        {"dickens", "the"},
        {"dickens", "Copperfield"},
        {"webster", "Gutenberg"},
        {"xml", "<leqgeq>"},
    }};
    for (const auto& [name, pattern] : cases)
    {
        const fs::path path = silesia / name;
        if (!fs::exists(path))
        {
            std::cerr << "No " << path.string() << ", skipping the search benchmarks\n";
            return;
        }
        const std::string suffix = std::string(name) + "/" + pattern;
        ::benchmark::RegisterBenchmark("Search/DecodeAndFind/" + suffix, decode_and_find, path,
                                       pattern)
            ->Unit(::benchmark::kMillisecond);
        ::benchmark::RegisterBenchmark("Search/BitScan/" + suffix, bit_scan, path, pattern)
            ->Unit(::benchmark::kMillisecond);
    }
}

} // namespace compressor::bench
