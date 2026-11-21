#include <metal_stdlib>
using namespace metal;

// Each thread counts a stretch of `size` bytes into threadgroup counters, then adds them to the
// global ones.
kernel void histogram(device const uchar* bytes [[buffer(0)]],
                      device atomic_uint* counts [[buffer(1)]],
                      constant uint& size [[buffer(2)]],
                      constant uint& stretch [[buffer(3)]],
                      uint thread_id [[thread_position_in_grid]],
                      uint local [[thread_position_in_threadgroup]],
                      uint group_size [[threads_per_threadgroup]])
{
    threadgroup atomic_uint local_counts[256];
    for (uint i = local; i < 256; i += group_size)
    {
        atomic_store_explicit(&local_counts[i], 0, memory_order_relaxed);
    }
    threadgroup_barrier(mem_flags::mem_threadgroup);
    const uint begin = thread_id * stretch;
    const uint end = min(begin + stretch, size);
    for (uint i = begin; i < end; ++i)
    {
        atomic_fetch_add_explicit(&local_counts[bytes[i]], 1, memory_order_relaxed);
    }
    threadgroup_barrier(mem_flags::mem_threadgroup);
    for (uint i = local; i < 256; i += group_size)
    {
        const uint count = atomic_load_explicit(&local_counts[i], memory_order_relaxed);
        if (count != 0)
        {
            atomic_fetch_add_explicit(&counts[i], count, memory_order_relaxed);
        }
    }
}

// LZT's match finder (lzt.cpp, ChainFinder::find) for every position at once: thread `at` walks its
// hash chain (`previous`: position + 1 of the previous position with the same hash, 0 for none)
// with the same rules, and writes up to `slots` (length, offset) pairs, each longer than the one
// before. Lengths are compared only up to nice + 1, so a long run costs no more than a short one;
// a position whose match reaches that cap or that has more matches than slots gets count 255, and
// the CPU finds its matches itself if they are needed.
kernel void lzt_matches(device const uchar* bytes [[buffer(0)]],
                        device const uint* previous [[buffer(1)]],
                        device uint2* matches [[buffer(2)]],
                        device uchar* counts [[buffer(3)]],
                        constant uint& size [[buffer(4)]],
                        constant uint& depth [[buffer(5)]],
                        constant uint& nice [[buffer(6)]],
                        constant uint& good [[buffer(7)]],
                        constant uint& slots [[buffer(8)]],
                        uint at [[thread_position_in_grid]])
{
    if (at >= size)
    {
        return;
    }
    if (at + 4 > size)
    {
        counts[at] = 0;
        return;
    }
    const uint limit = size - at;
    const uint cap = min(limit, nice + 1);
    uint candidate = previous[at];
    uint best = 2;
    uint steps = depth;
    uint count = 0;
    for (uint step = 0; step < steps && candidate != 0; ++step)
    {
        const uint from = candidate - 1;
        if (bytes[from + best] == bytes[at + best])
        {
            uint length = 0;
            while (length < cap && bytes[from + length] == bytes[at + length])
            {
                ++length;
            }
            if (length > best)
            {
                if (count == slots || (length == cap && cap < limit))
                {
                    counts[at] = 255;
                    return;
                }
                best = length;
                matches[(ulong(at) * slots) + count] = uint2(length, at - from);
                ++count;
                if (length >= nice || length == limit)
                {
                    break;
                }
                if (length >= good)
                {
                    steps = min(steps, step + 1 + (depth / 4));
                }
            }
        }
        candidate = previous[from];
    }
    counts[at] = uchar(count);
}
