#include <compressor/bwt/bwt.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <numeric>
#include <span>
#include <utility>
#include <vector>

#include <compressor/entropy/rans.h>
#include <compressor/error.h>

namespace compressor
{

namespace
{

// Symbols after move-to-front: RUNA and RUNB write a run of zeros in bijective base 2; MTF index
// i (1-255) becomes i + 1.
constexpr uint16_t run_a = 0;
constexpr uint16_t run_b = 1;
constexpr std::size_t alphabet = 257;

std::unexpected<Error> corrupt()
{
    return std::unexpected(Error::corrupt_payload);
}

void put_le32(std::vector<uint8_t>& out, uint32_t value)
{
    for (unsigned i = 0; i < 4; ++i)
    {
        out.push_back(static_cast<uint8_t>(value >> (8 * i)));
    }
}

uint32_t get_le32(std::span<const uint8_t> bytes)
{
    return static_cast<uint32_t>(bytes[0]) | (static_cast<uint32_t>(bytes[1]) << 8U) |
           (static_cast<uint32_t>(bytes[2]) << 16U) | (static_cast<uint32_t>(bytes[3]) << 24U);
}

void write_run(std::vector<uint16_t>& symbols, std::size_t run)
{
    // Bijective base 2: digits 1 (RUNA) and 2 (RUNB), least significant first.
    while (run > 0)
    {
        if ((run & 1U) != 0)
        {
            symbols.push_back(run_a);
            run = (run - 1) / 2;
        }
        else
        {
            symbols.push_back(run_b);
            run = (run - 2) / 2;
        }
    }
}

std::vector<uint16_t> move_to_front(std::span<const uint8_t> bytes)
{
    std::array<uint8_t, 256> order{};
    std::ranges::iota(order, uint8_t{0});
    std::vector<uint16_t> symbols;
    symbols.reserve(bytes.size());
    std::size_t zeros = 0;
    for (const uint8_t byte : bytes)
    {
        const auto at = static_cast<std::size_t>(std::ranges::find(order, byte) - order.begin());
        if (at == 0)
        {
            ++zeros;
            continue;
        }
        write_run(symbols, zeros);
        zeros = 0;
        std::copy_backward(order.begin(), order.begin() + static_cast<std::ptrdiff_t>(at),
                           order.begin() + static_cast<std::ptrdiff_t>(at) + 1);
        order[0] = byte;
        symbols.push_back(static_cast<uint16_t>(at + 1));
    }
    write_run(symbols, zeros);
    return symbols;
}

} // namespace

namespace
{

// SA-IS (Nong, Zhang and Chan, 2009): suffixes are classed S or L by whether they sort before or
// after the next one, the leftmost S suffixes (LMS) are sorted by inducing from bucket edges, and
// when their substrings aren't all different, the reduced string of their names is sorted
// recursively. `s` ends with a unique smallest symbol 0; symbols are below `k`.
void get_buckets(std::span<const int32_t> s, std::span<int32_t> buckets, bool ends)
{
    std::ranges::fill(buckets, 0);
    for (const int32_t c : s)
    {
        ++buckets[static_cast<std::size_t>(c)];
    }
    int32_t sum = 0;
    for (int32_t& bucket : buckets)
    {
        sum += bucket;
        bucket = ends ? sum : sum - bucket;
    }
}

void induce(std::span<const int32_t> s, std::span<int32_t> sa, const std::vector<uint8_t>& s_type,
            std::span<int32_t> buckets)
{
    const std::size_t n = s.size();
    get_buckets(s, buckets, false);
    for (std::size_t i = 0; i < n; ++i)
    {
        const int32_t j = sa[i] - 1;
        if (sa[i] > 0 && s_type[static_cast<std::size_t>(j)] == 0)
        {
            sa[static_cast<std::size_t>(
                buckets[static_cast<std::size_t>(s[static_cast<std::size_t>(j)])]++)] = j;
        }
    }
    get_buckets(s, buckets, true);
    for (std::size_t i = n; i-- > 0;)
    {
        const int32_t j = sa[i] - 1;
        if (sa[i] > 0 && s_type[static_cast<std::size_t>(j)] != 0)
        {
            sa[static_cast<std::size_t>(
                --buckets[static_cast<std::size_t>(s[static_cast<std::size_t>(j)])])] = j;
        }
    }
}

void sa_is(std::span<const int32_t> s, std::span<int32_t> sa, std::size_t k)
{
    const std::size_t n = s.size();
    std::vector<uint8_t> s_type(n, 0); // bytes rather than vector<bool>.s bits
    s_type[n - 1] = 1;
    for (std::size_t i = n - 1; i-- > 0;)
    {
        s_type[i] =
            static_cast<uint8_t>(s[i] < s[i + 1] || (s[i] == s[i + 1] && s_type[i + 1] != 0));
    }
    const auto lms = [&](std::size_t i) {
        return i > 0 && s_type[i] != 0 && s_type[i - 1] == 0;
    };
    std::vector<int32_t> buckets(k);

    // Sort the LMS substrings: place the LMS positions at their bucket ends and induce.
    get_buckets(s, buckets, true);
    std::ranges::fill(sa, -1);
    for (std::size_t i = 1; i < n; ++i)
    {
        if (lms(i))
        {
            sa[static_cast<std::size_t>(--buckets[static_cast<std::size_t>(s[i])])] =
                static_cast<int32_t>(i);
        }
    }
    induce(s, sa, s_type, buckets);

    // Name them in that order; equal substrings share a name.
    std::size_t lms_count = 0;
    for (std::size_t i = 0; i < n; ++i)
    {
        if (lms(static_cast<std::size_t>(sa[i])))
        {
            sa[lms_count++] = sa[i];
        }
    }
    std::fill(sa.begin() + static_cast<std::ptrdiff_t>(lms_count), sa.end(), -1);
    int32_t names = 0;
    std::size_t previous = n;
    for (std::size_t i = 0; i < lms_count; ++i)
    {
        const auto position = static_cast<std::size_t>(sa[i]);
        bool different = previous == n;
        for (std::size_t d = 0; !different; ++d)
        {
            if (s[position + d] != s[previous + d] || s_type[position + d] != s_type[previous + d])
            {
                different = true;
            }
            else if (d > 0 && (lms(position + d) || lms(previous + d)))
            {
                break;
            }
        }
        if (different)
        {
            ++names;
            previous = position;
        }
        sa[lms_count + (position / 2)] = names - 1;
    }
    for (std::size_t i = n, j = n; i-- > lms_count;)
    {
        if (sa[i] >= 0)
        {
            sa[--j] = sa[i];
        }
    }

    // Sort the LMS suffixes: by recursion unless every name is different.
    const std::span<int32_t> reduced = sa.subspan(n - lms_count);
    const std::span<int32_t> reduced_sa = sa.first(lms_count);
    if (std::cmp_less(names, lms_count))
    {
        sa_is(reduced, reduced_sa, static_cast<std::size_t>(names));
    }
    else
    {
        for (std::size_t i = 0; i < lms_count; ++i)
        {
            reduced_sa[static_cast<std::size_t>(reduced[i])] = static_cast<int32_t>(i);
        }
    }

    // Induce the whole order from the sorted LMS suffixes.
    for (std::size_t i = 1, j = 0; i < n; ++i)
    {
        if (lms(i))
        {
            reduced[j++] = static_cast<int32_t>(i);
        }
    }
    for (std::size_t i = 0; i < lms_count; ++i)
    {
        reduced_sa[i] = reduced[static_cast<std::size_t>(reduced_sa[i])];
    }
    std::fill(sa.begin() + static_cast<std::ptrdiff_t>(lms_count), sa.end(), -1);
    get_buckets(s, buckets, true);
    for (std::size_t i = lms_count; i-- > 0;)
    {
        const int32_t j = sa[i];
        sa[i] = -1;
        sa[static_cast<std::size_t>(
            --buckets[static_cast<std::size_t>(s[static_cast<std::size_t>(j)])])] = j;
    }
    induce(s, sa, s_type, buckets);
}

} // namespace

std::vector<uint32_t> suffix_array(std::span<const uint8_t> bytes)
{
    const std::size_t n = bytes.size();
    std::vector<uint32_t> result(n);
    if (n == 0)
    {
        return result;
    }
    // A unique smallest end symbol, so a suffix sorts before every longer one it prefixes.
    std::vector<int32_t> s(n + 1);
    for (std::size_t i = 0; i < n; ++i)
    {
        s[i] = int32_t{bytes[i]} + 1;
    }
    s[n] = 0;
    std::vector<int32_t> sa(n + 1);
    sa_is(s, sa, 257);
    // sa[0] is the end symbol's own suffix.
    for (std::size_t i = 0; i < n; ++i)
    {
        result[i] = static_cast<uint32_t>(sa[i + 1]);
    }
    return result;
}

uint32_t forward_bwt(std::span<const uint8_t> bytes, std::vector<uint8_t>& out)
{
    const std::vector<uint32_t> sa = suffix_array(bytes);
    out.clear();
    out.reserve(bytes.size());
    // With the end marker as a virtual first row, the suffix starting at 0 is preceded by it.
    uint32_t primary = 0;
    out.push_back(bytes.empty() ? 0 : bytes.back()); // the row of the end marker itself
    for (std::size_t row = 0; row < sa.size(); ++row)
    {
        if (sa[row] == 0)
        {
            primary = static_cast<uint32_t>(row + 1);
            continue;
        }
        out.push_back(bytes[sa[row] - 1]);
    }
    if (bytes.empty())
    {
        out.clear();
    }
    return primary;
}

std::expected<void, Error> inverse_bwt(std::span<const uint8_t> transformed, uint32_t primary,
                                       std::vector<uint8_t>& out)
{
    const std::size_t n = transformed.size();
    if (n == 0)
    {
        return primary == 0 ? std::expected<void, Error>{} : corrupt();
    }
    if (primary == 0 || primary > n)
    {
        return corrupt();
    }
    // Rows 0..n: row 0 is the end marker's, and `primary` holds the marker in the last column.
    // transformed[r] is the last column of row r for r < primary, of row r + 1 after it.
    std::array<uint32_t, 256> count{};
    for (const uint8_t byte : transformed)
    {
        ++count[byte];
    }
    std::array<uint32_t, 256> first{};
    uint32_t total = 1; // the end marker sorts first
    for (std::size_t c = 0; c < 256; ++c)
    {
        first[c] = total;
        total += count[c];
    }
    // lf[row] for every row with a real byte in the last column.
    std::vector<uint32_t> lf(n + 1);
    std::array<uint32_t, 256> seen{};
    for (std::size_t row = 0; row <= n; ++row)
    {
        if (row == primary)
        {
            continue;
        }
        const uint8_t byte = transformed[row < primary ? row : row - 1];
        lf[row] = first[byte] + seen[byte]++;
    }
    const std::size_t start = out.size();
    out.resize(start + n);
    // Walking LF from row 0 (whose last column is the input's last byte) yields the input
    // backwards.
    std::size_t row = 0;
    for (std::size_t i = n; i-- > 0;)
    {
        if (row == primary)
        {
            return corrupt();
        }
        out[start + i] = transformed[row < primary ? row : row - 1];
        row = lf[row];
    }
    if (row != primary)
    {
        out.resize(start);
        return corrupt();
    }
    return {};
}

namespace
{

// Undoes the zero runs and move-to-front.
std::expected<void, Error> undo_move_to_front(std::span<const uint16_t> symbols,
                                              std::vector<uint8_t>& transformed, std::size_t size)
{
    transformed.clear();
    transformed.reserve(size);
    std::array<uint8_t, 256> order{};
    std::ranges::iota(order, uint8_t{0});
    std::size_t run = 0;
    std::size_t weight = 1;
    const auto flush_run = [&]() {
        if (run > size - transformed.size())
        {
            return false;
        }
        transformed.insert(transformed.end(), run, order[0]);
        run = 0;
        weight = 1;
        return true;
    };
    for (const uint16_t symbol : symbols)
    {
        if (symbol == run_a || symbol == run_b)
        {
            run += weight * (symbol == run_a ? 1 : 2);
            weight *= 2;
            if (run > size)
            {
                return corrupt();
            }
            continue;
        }
        if (symbol >= alphabet || !flush_run() || transformed.size() >= size)
        {
            return corrupt();
        }
        const std::size_t at = symbol - 1U;
        const uint8_t byte = order[at];
        std::copy_backward(order.begin(), order.begin() + static_cast<std::ptrdiff_t>(at),
                           order.begin() + static_cast<std::ptrdiff_t>(at) + 1);
        order[0] = byte;
        transformed.push_back(byte);
    }
    if (!flush_run() || transformed.size() != size)
    {
        return corrupt();
    }
    return {};
}

std::expected<void, Error> rebuild(std::span<const uint16_t> symbols, uint32_t primary,
                                   std::vector<uint8_t>& out, std::size_t size)
{
    std::vector<uint8_t> transformed;
    if (auto undone = undo_move_to_front(symbols, transformed, size); !undone)
    {
        return undone;
    }
    return inverse_bwt(transformed, primary, out);
}

} // namespace

std::vector<uint8_t> bwt_encode(std::span<const uint8_t> bytes)
{
    std::vector<uint8_t> transformed;
    const uint32_t primary = forward_bwt(bytes, transformed);
    return bwt_encode_transformed(transformed, primary);
}

std::vector<uint8_t> bwt_encode_transformed(std::span<const uint8_t> transformed, uint32_t primary)
{
    const std::vector<uint16_t> symbols = move_to_front(transformed);
    std::vector<uint8_t> out;
    put_le32(out, primary);
    put_le32(out, static_cast<uint32_t>(symbols.size()));
    rans_encode(symbols, alphabet, out);
    return out;
}

std::expected<void, Error> bwt_decode(std::span<const uint8_t> payload, std::vector<uint8_t>& out,
                                      std::size_t size)
{
    if (payload.size() < 8)
    {
        return corrupt();
    }
    const uint32_t primary = get_le32(payload);
    const uint32_t symbol_count = get_le32(payload.subspan(4));
    if (symbol_count > size)
    {
        return corrupt();
    }
    auto symbols = rans_decode(payload.subspan(8), alphabet, symbol_count);
    if (!symbols)
    {
        return std::unexpected(symbols.error());
    }
    return rebuild(*symbols, primary, out, size);
}

std::expected<uint32_t, Error> bwt_decode_transformed(std::span<const uint8_t> payload,
                                                      std::vector<uint8_t>& transformed,
                                                      std::size_t size)
{
    if (payload.size() < 8)
    {
        return corrupt();
    }
    const uint32_t primary = get_le32(payload);
    const uint32_t symbol_count = get_le32(payload.subspan(4));
    if (symbol_count > size || primary > size || (size > 0 && primary == 0))
    {
        return corrupt();
    }
    auto symbols = rans_decode(payload.subspan(8), alphabet, symbol_count);
    if (!symbols)
    {
        return std::unexpected(symbols.error());
    }
    if (auto undone = undo_move_to_front(*symbols, transformed, size); !undone)
    {
        return std::unexpected(undone.error());
    }
    return primary;
}

} // namespace compressor
