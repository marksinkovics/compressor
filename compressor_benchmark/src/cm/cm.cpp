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
#include <compressor/lz77/lz77.h>
#include <compressor/lzr/lzr.h>

namespace compressor::bench
{

namespace
{

namespace fs = std::filesystem;

constexpr unsigned orders = 6; // contexts of the last 1, 2, 3, 4, 5 and 6 bytes
constexpr unsigned inputs = orders + 1;
constexpr unsigned table_bits = 20;

int squash(int x) // 1 / (1 + e^-x), x and the result scaled by 2^8 and 2^12
{
    if (x > 2047)
    {
        return 4095;
    }
    if (x < -2047)
    {
        return 1;
    }
    const double v = 4096.0 / (1.0 + std::exp(-x / 256.0));
    return std::clamp(static_cast<int>(v), 1, 4095);
}

class Stretch
{
public:
    Stretch()
    {
        int pi = 0;
        for (int x = -2047; x <= 2047; ++x)
        {
            const int v = squash(x);
            for (int i = pi; i <= v; ++i)
            {
                table_[static_cast<std::size_t>(i)] = static_cast<int16_t>(x);
            }
            pi = v + 1;
        }
        for (int i = pi; i < 4096; ++i)
        {
            table_[static_cast<std::size_t>(i)] = 2047;
        }
    }

    int operator()(int p) const
    {
        return table_[static_cast<std::size_t>(p)];
    }

private:
    std::array<int16_t, 4096> table_{};
};

const Stretch stretch;

class Predictor
{
public:
    Predictor()
        : tables_(orders, std::vector<uint16_t>(std::size_t{1} << table_bits, 1U << 15U))
        , counts_(orders, std::vector<uint8_t>(std::size_t{1} << table_bits, 0))
        , weights_(std::size_t{256} * inputs, 16384)
    {
        new_byte();
    }

    int p() // probability of a 1, 12 bits
    {
        int dot = 0;
        const std::size_t set = std::size_t{partial_} * inputs;
        for (unsigned i = 0; i < orders; ++i)
        {
            const std::size_t slot =
                (hashes_[i] + (partial_ * 0x9E3779B1U)) & ((1U << table_bits) - 1);
            slots_[i] = slot;
            input_[i] = stretch(tables_[i][slot] >> 4U);
        }
        input_[orders] = 256;
        for (unsigned i = 0; i < inputs; ++i)
        {
            dot += (input_[i] * weights_[set + i]) / 65536;
        }
        prediction_ = squash(dot);
        return prediction_;
    }

    void update(unsigned bit)
    {
        const int error = ((static_cast<int>(bit) * 4096) - prediction_) * 6;
        const std::size_t set = std::size_t{partial_} * inputs;
        for (unsigned i = 0; i < inputs; ++i)
        {
            weights_[set + i] += (input_[i] * error) / 1024;
        }
        for (unsigned i = 0; i < orders; ++i)
        {
            uint16_t& probability = tables_[i][slots_[i]];
            uint8_t& count = counts_[i][slots_[i]];
            const int target = bit != 0 ? 65535 : 0;
            probability =
                static_cast<uint16_t>(probability + ((target - probability) / (count + 2)));
            count = std::min<uint8_t>(count + 1, 30);
        }
        partial_ = (partial_ << 1U) | bit;
        if (partial_ >= 256)
        {
            history_ = (history_ << 8U) | (partial_ & 0xFFU);
            new_byte();
        }
    }

private:
    void new_byte()
    {
        partial_ = 1;
        for (unsigned i = 0; i < orders; ++i)
        {
            const uint64_t mask = (uint64_t{1} << (8 * (i + 1))) - 1;
            hashes_[i] =
                static_cast<uint32_t>(((history_ & mask) + i + 1) * 0xD6E8FEB86659FD93ULL >> 32U);
        }
    }

    std::vector<std::vector<uint16_t>> tables_;
    std::vector<std::vector<uint8_t>> counts_;
    std::vector<int> weights_;
    std::array<uint32_t, orders> hashes_{};
    std::array<std::size_t, orders> slots_{};
    std::array<int, inputs> input_{};
    uint64_t history_ = 0;
    uint32_t partial_ = 1;
    int prediction_ = 2048;
};

} // namespace

std::vector<uint8_t> cm_encode(std::span<const uint8_t> bytes)
{
    std::vector<uint8_t> out;
    Predictor predictor;
    uint32_t low = 0;
    uint32_t high = 0xFFFFFFFFU;
    for (const uint8_t byte : bytes)
    {
        for (unsigned i = 8; i-- > 0;)
        {
            const unsigned bit = (unsigned{byte} >> i) & 1U;
            const uint32_t mid =
                low + static_cast<uint32_t>(
                          (uint64_t{high - low} * static_cast<uint32_t>(predictor.p())) >> 12U);
            if (bit != 0)
            {
                high = mid;
            }
            else
            {
                low = mid + 1;
            }
            predictor.update(bit);
            while (((low ^ high) & 0xFF000000U) == 0)
            {
                out.push_back(static_cast<uint8_t>(high >> 24U));
                low <<= 8U;
                high = (high << 8U) | 0xFFU;
            }
        }
    }
    for (int i = 0; i < 4; ++i)
    {
        out.push_back(static_cast<uint8_t>(low >> 24U));
        low <<= 8U;
    }
    return out;
}

std::vector<uint8_t> cm_decode(std::span<const uint8_t> coded, std::size_t size)
{
    std::vector<uint8_t> out;
    out.reserve(size);
    Predictor predictor;
    uint32_t low = 0;
    uint32_t high = 0xFFFFFFFFU;
    uint32_t x = 0;
    std::size_t next = 0;
    const auto byte_in = [&] {
        return next < coded.size() ? coded[next++] : uint8_t{0};
    };
    for (int i = 0; i < 4; ++i)
    {
        x = (x << 8U) | byte_in();
    }
    while (out.size() < size)
    {
        uint32_t byte = 0;
        for (int i = 0; i < 8; ++i)
        {
            const uint32_t mid =
                low + static_cast<uint32_t>(
                          (uint64_t{high - low} * static_cast<uint32_t>(predictor.p())) >> 12U);
            const unsigned bit = x <= mid ? 1U : 0U;
            if (bit != 0)
            {
                high = mid;
            }
            else
            {
                low = mid + 1;
            }
            predictor.update(bit);
            byte = (byte << 1U) | bit;
            while (((low ^ high) & 0xFF000000U) == 0)
            {
                low <<= 8U;
                high = (high << 8U) | 0xFFU;
                x = (x << 8U) | byte_in();
            }
        }
        out.push_back(static_cast<uint8_t>(byte));
    }
    return out;
}

namespace
{

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
        if (cm_decode(coded, block.size()) != std::vector<uint8_t>(block.begin(), block.end()))
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
    for (const char* name : {"dickens", "xml", "samba", "sao", "x-ray", "ooffice"})
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
