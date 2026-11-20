#include <compressor/lz77/lz77.h>

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <vector>

namespace compressor
{

namespace
{

constexpr unsigned hash_bits = 15;
constexpr std::size_t hash_size = std::size_t{1} << hash_bits;

} // namespace

Lz77Options lz77_options_for_level(unsigned level)
{
    // zlib's table for its levels 1, 3 and 4-9.
    switch (level)
    {
    case 0:
    case 1:
    case 2:
        return {.max_chain = 4, .nice_length = 8, .lazy = false, .max_lazy = 0, .good_length = 4};
    case 3:
        return {.max_chain = 32, .nice_length = 32, .lazy = false, .max_lazy = 0, .good_length = 4};
    case 4:
        return {.max_chain = 16, .nice_length = 16, .lazy = true, .max_lazy = 4, .good_length = 4};
    case 5:
        return {.max_chain = 32, .nice_length = 32, .lazy = true, .max_lazy = 16, .good_length = 8};
    case 6:
        return {};
    case 7:
        return {
            .max_chain = 256, .nice_length = 128, .lazy = true, .max_lazy = 32, .good_length = 8};
    case 8:
        return {.max_chain = 1024,
                .nice_length = 258,
                .lazy = true,
                .max_lazy = 128,
                .good_length = 32};
    default:
        return {.max_chain = 4096,
                .nice_length = 258,
                .lazy = true,
                .max_lazy = 258,
                .good_length = 32};
    }
}

Lz77MatchFinder::Lz77MatchFinder(Lz77Options options)
    : options_(options)
    , head_(hash_size, -1)
    , previous_(options.window, -1)
{
}

void Lz77MatchFinder::add(std::span<const uint8_t> bytes)
{
    data_.insert(data_.end(), bytes.begin(), bytes.end());
}

void Lz77MatchFinder::prime(std::span<const uint8_t> bytes)
{
    add(bytes);
    const uint64_t end = base_ + data_.size();
    for (; position_ < end; ++position_)
    {
        insert(position_);
    }
}

uint64_t Lz77MatchFinder::position() const
{
    return position_;
}

std::span<const uint8_t> Lz77MatchFinder::history(uint64_t from) const
{
    from = std::max(from, base_);
    return {at_ptr(from), static_cast<std::size_t>(position_ - from)};
}

void Lz77MatchFinder::drop_old(uint64_t keep_from)
{
    const uint64_t reachable = position_ > options_.window ? position_ - options_.window : 0;
    const uint64_t new_base = std::min(keep_from, reachable);
    if (new_base <= base_)
    {
        return;
    }
    data_.erase(data_.begin(), data_.begin() + static_cast<std::ptrdiff_t>(new_base - base_));
    base_ = new_base;
}

const uint8_t* Lz77MatchFinder::at_ptr(uint64_t at) const
{
    return data_.data() + (at - base_);
}

uint32_t Lz77MatchFinder::hash_at(uint64_t at) const
{
    const uint8_t* p = at_ptr(at);
    return ((uint32_t{p[0]} << 10U) ^ (uint32_t{p[1]} << 5U) ^ p[2]) & (hash_size - 1);
}

void Lz77MatchFinder::insert(uint64_t at)
{
    if (at + lz77_min_match > base_ + data_.size())
    {
        return;
    }
    const uint32_t hash = hash_at(at);
    previous_[at % options_.window] = head_[hash];
    head_[hash] = static_cast<int64_t>(at);
}

// How many bytes from `a` and `b` agree, up to `limit`, 8 at a time.
std::size_t common_length(const uint8_t* a, const uint8_t* b, std::size_t limit)
{
    std::size_t length = 0;
    while (length + 8 <= limit)
    {
        uint64_t x = 0;
        uint64_t y = 0;
        std::memcpy(&x, a + length, 8);
        std::memcpy(&y, b + length, 8);
        if (x != y)
        {
            return length + (static_cast<std::size_t>(std::countr_zero(x ^ y)) / 8);
        }
        length += 8;
    }
    while (length < limit && a[length] == b[length])
    {
        ++length;
    }
    return length;
}

std::size_t Lz77MatchFinder::longest_match(uint64_t at, std::size_t limit, unsigned chain,
                                           uint32_t& distance) const
{
    if (limit < lz77_min_match)
    {
        return 0;
    }
    const uint8_t* here = at_ptr(at);
    std::size_t best = lz77_min_match - 1;
    int64_t candidate = head_[hash_at(at)];
    for (; chain > 0 && candidate >= 0; --chain)
    {
        const auto from = static_cast<uint64_t>(candidate);
        if (at - from > options_.window || from < base_)
        {
            break;
        }
        const uint8_t* there = at_ptr(from);
        if (there[best] == here[best] && there[0] == here[0] && there[1] == here[1])
        {
            const std::size_t length = 2 + common_length(there + 2, here + 2, limit - 2);
            if (length > best)
            {
                best = length;
                distance = static_cast<uint32_t>(at - from);
                if (length >= options_.nice_length || length == limit)
                {
                    break;
                }
            }
        }
        const int64_t next = previous_[from % options_.window];
        // A stale entry from a position that left the window, or one overwritten since.
        if (next >= candidate)
        {
            break;
        }
        candidate = next;
    }
    return best >= lz77_min_match ? best : 0;
}

void Lz77MatchFinder::tokenize(std::vector<Lz77Token>& tokens, bool final)
{
    static_assert(std::endian::native == std::endian::little);
    const uint64_t end = base_ + data_.size();
    const uint64_t stop = final ? end : (end > lz77_max_match ? end - (lz77_max_match - 1) : 0);
    const auto limit_at = [end](uint64_t at) {
        return static_cast<std::size_t>(std::min<uint64_t>(lz77_max_match, end - at));
    };
    while (position_ < stop)
    {
        uint32_t distance = 0;
        std::size_t length = 0;
        if (have_next_)
        {
            length = next_length_;
            distance = next_distance_;
            have_next_ = false;
        }
        else
        {
            length = longest_match(position_, limit_at(position_), options_.max_chain, distance);
        }
        insert(position_);
        if (length == 0)
        {
            tokens.push_back({.length_or_literal = *at_ptr(position_), .distance = 0});
            ++position_;
            continue;
        }
        if (options_.lazy && length < options_.max_lazy && position_ + 1 < stop)
        {
            const unsigned chain =
                length >= options_.good_length ? options_.max_chain / 4 : options_.max_chain;
            uint32_t next_distance = 0;
            const std::size_t next = longest_match(position_ + 1, limit_at(position_ + 1),
                                                   std::max(chain, 1U), next_distance);
            if (next > length)
            {
                tokens.push_back({.length_or_literal = *at_ptr(position_), .distance = 0});
                ++position_;
                have_next_ = true;
                next_length_ = next;
                next_distance_ = next_distance;
                continue;
            }
        }
        tokens.push_back(
            {.length_or_literal = static_cast<uint16_t>(length), .distance = distance});
        for (uint64_t covered = position_ + 1; covered < position_ + length; ++covered)
        {
            insert(covered);
        }
        position_ += length;
    }
}

} // namespace compressor
