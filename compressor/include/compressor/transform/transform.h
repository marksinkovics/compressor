#ifndef Transform_h
#define Transform_h

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace compressor
{

// Byte-to-byte transforms a pipeline block can run before its coder. The values are stored in the
// file (docs/file_format.md, "Pipeline blocks").
enum class TransformId : uint8_t
{
    delta = 1,        // each byte minus the one `parameter` bytes before it
    arm64_branch = 3, // BL targets made absolute, so repeated calls to one function match
    x86_branch = 4,   // the same for E8/E9 call and jump targets
};

struct Transform
{
    TransformId id = TransformId::delta;
    uint32_t parameter = 0;

    bool operator==(const Transform&) const = default;
};

inline constexpr uint32_t max_delta_stride = 65535;

// The transform a descriptor names, if it is a known id with a valid parameter.
std::optional<Transform> parse_transform(uint8_t id, uint32_t parameter);
// In place. Positions count from the start of `bytes`, so each block is transformed on its own.
void apply_transform(const Transform& transform, std::span<uint8_t> bytes);
void undo_transform(const Transform& transform, std::span<uint8_t> bytes);

// Transforms likely to help `bytes`, best first, each to be tried on its own: by sampling, delta
// strides that lower the order-0 entropy by a tenth, branch filters when calls are dense enough to
// be machine code. At most one unless `thorough`, which also returns runners-up and looser
// matches (a twentieth less entropy, half the call density).
std::vector<Transform> choose_transforms(std::span<const uint8_t> bytes, bool thorough = false);

} // namespace compressor

#endif /* Transform_h */
