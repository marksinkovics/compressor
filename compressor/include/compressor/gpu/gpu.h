#ifndef Gpu_h
#define Gpu_h

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <span>
#include <string_view>
#include <utility>

namespace compressor::gpu
{

// The GPU through Metal (macOS, built with COMPRESSOR_METAL): buffers in memory the CPU and GPU
// share, so nothing is copied, and the kernels in compressor/src/compressor/gpu/kernels.metal,
// called by name. Every caller keeps a CPU path; without a GPU, device() is null.
class Buffer
{
public:
    virtual ~Buffer() = default;
    virtual std::span<uint8_t> bytes() = 0;

    template <class T> std::span<T> as()
    {
        const std::span<uint8_t> raw = bytes();
        return {reinterpret_cast<T*>(raw.data()), raw.size() / sizeof(T)};
    }
};

// A kernel's arguments in order: buffers, or small values passed by value.
struct Argument
{
    Buffer* buffer = nullptr;
    std::span<const uint8_t> value; // when buffer is null

    static Argument of(Buffer& buffer)
    {
        return {.buffer = &buffer, .value = {}};
    }

    template <class T> static Argument of_value(const T& value)
    {
        return {.buffer = nullptr, .value = {reinterpret_cast<const uint8_t*>(&value), sizeof(T)}};
    }
};

class Device
{
public:
    virtual ~Device() = default;
    virtual std::string_view name() const = 0;
    virtual std::unique_ptr<Buffer> buffer(std::size_t size) = 0;
    // Runs `kernel` over `threads` threads (in groups of the kernel's preferred size) and waits.
    // False when there is no such kernel or the GPU reports an error.
    virtual bool run(std::string_view kernel, std::size_t threads,
                     std::initializer_list<Argument> arguments) = 0;
};

// The system's GPU, created once; null without one or without COMPRESSOR_METAL, or when the
// environment variable COMPRESSOR_NO_GPU is set (to compare with the CPU path).
Device* device();

// The byte histogram of `bytes`, on the GPU when there is one: the first kernel, for tests and
// benchmarks of the plumbing.
void histogram(std::span<const uint8_t> bytes, std::span<uint32_t, 256> counts);

// LZT's match lists for every position of `bytes` (kernels.metal, lzt_matches): `previous` holds
// each position's previous position with the same hash, plus 1. `matches` gets `slots` pairs per
// position and `counts` their number, or 255 where the CPU must search itself. False without a GPU.
inline constexpr unsigned no_gpu_matches = 255;
bool lzt_matches(std::span<const uint8_t> bytes, std::span<const uint32_t> previous, unsigned depth,
                 unsigned nice, unsigned good, unsigned slots,
                 std::span<std::pair<uint32_t, uint32_t>> matches, std::span<uint8_t> counts);

} // namespace compressor::gpu

#endif /* Gpu_h */
