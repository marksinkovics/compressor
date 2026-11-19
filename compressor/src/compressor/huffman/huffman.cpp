#include <compressor/huffman/huffman.h>

#include <algorithm>
#include <vector>

namespace compressor
{

namespace
{

struct Item
{
    uint64_t weight;
    int symbol; // -1 for a package
};

} // namespace

CodeLengths code_lengths(const Frequencies& frequencies, std::size_t max_length)
{
    CodeLengths lengths{};

    std::vector<Item> leaves;
    for (std::size_t symbol = 0; symbol < symbol_count; ++symbol)
    {
        if (frequencies[symbol] > 0)
        {
            leaves.push_back({.weight = frequencies[symbol], .symbol = static_cast<int>(symbol)});
        }
    }
    if (leaves.empty())
    {
        return lengths;
    }
    if (leaves.size() == 1)
    {
        lengths[static_cast<std::size_t>(leaves.front().symbol)] = 1;
        return lengths;
    }
    std::ranges::stable_sort(leaves, {}, &Item::weight);

    // levels[0] is the shallowest level (codes of length 1), levels[max_length - 1] the deepest.
    // Each level is the leaves merged with the packages (adjacent pairs) of the level below it.
    std::vector<std::vector<Item>> levels(max_length);
    levels[max_length - 1] = leaves;
    for (std::size_t level = max_length - 1; level-- > 0;)
    {
        const std::vector<Item>& deeper = levels[level + 1];
        std::vector<Item> packages;
        for (std::size_t i = 0; i + 1 < deeper.size(); i += 2)
        {
            packages.push_back({.weight = deeper[i].weight + deeper[i + 1].weight, .symbol = -1});
        }
        std::vector<Item>& merged = levels[level];
        merged.reserve(leaves.size() + packages.size());
        // Leaves first on equal weight, so the result is deterministic.
        std::ranges::merge(leaves, packages, std::back_inserter(merged), {}, &Item::weight,
                           &Item::weight);
    }

    // The 2(n-1) cheapest items of the top level form the code. The packages chosen on one level
    // are always the cheapest ones, so they cover a prefix of the level below; each leaf in a
    // chosen prefix adds one to its symbol's length.
    std::size_t take = 2 * (leaves.size() - 1);
    for (std::size_t level = 0; level < max_length && take > 0; ++level)
    {
        std::size_t packages = 0;
        const std::vector<Item>& items = levels[level];
        for (std::size_t i = 0; i < take && i < items.size(); ++i)
        {
            if (items[i].symbol < 0)
            {
                ++packages;
            }
            else
            {
                ++lengths[static_cast<std::size_t>(items[i].symbol)];
            }
        }
        take = 2 * packages;
    }
    return lengths;
}

Codes canonical_codes(const CodeLengths& lengths)
{
    std::array<uint32_t, max_code_length + 1> count_per_length{};
    for (const uint8_t length : lengths)
    {
        if (length > 0 && length <= max_code_length)
        {
            ++count_per_length[length];
        }
    }

    std::array<uint32_t, max_code_length + 1> next_code{};
    uint32_t code = 0;
    for (std::size_t length = 1; length <= max_code_length; ++length)
    {
        code = (code + count_per_length[length - 1]) << 1U;
        next_code[length] = code;
    }

    Codes codes{};
    for (std::size_t symbol = 0; symbol < symbol_count; ++symbol)
    {
        const uint8_t length = lengths[symbol];
        if (length > 0 && length <= max_code_length)
        {
            codes[symbol] = {.bits = next_code[length]++, .length = length};
        }
    }
    return codes;
}

bool valid_code_lengths(const CodeLengths& lengths)
{
    uint64_t kraft_sum = 0; // in units of 2^-max_code_length
    std::size_t used = 0;
    for (const uint8_t length : lengths)
    {
        if (length == 0)
        {
            continue;
        }
        if (length > max_code_length)
        {
            return false;
        }
        kraft_sum += uint64_t{1} << (max_code_length - length);
        ++used;
    }
    if (used == 0)
    {
        return true;
    }
    if (used == 1)
    {
        return kraft_sum == uint64_t{1} << (max_code_length - 1);
    }
    return kraft_sum == uint64_t{1} << max_code_length;
}

} // namespace compressor
