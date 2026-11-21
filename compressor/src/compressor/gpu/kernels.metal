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
