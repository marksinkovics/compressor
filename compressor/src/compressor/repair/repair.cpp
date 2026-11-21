#include <compressor/repair/repair.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <ios>
#include <istream>
#include <ostream>
#include <span>
#include <utility>
#include <vector>

#include <compressor/checksum/crc32.h>
#include <compressor/error.h>

namespace compressor
{

namespace
{

constexpr std::array<uint8_t, 4> magic{0x89, 'I', 'N', 'R'};
constexpr uint8_t version = 1;
constexpr std::size_t fixed_header_bytes = 4 + 1 + 8 + 8 + 1 + 1;
constexpr uint64_t min_shard_size = uint64_t{64} * 1024;
constexpr unsigned max_data_shards = 200;
constexpr std::size_t stripe = std::size_t{64} * 1024;

// GF(2^8) with the polynomial x^8 + x^4 + x^3 + x^2 + 1 (0x11D), generator 2.
struct Field
{
    std::array<uint8_t, 512> exp{};
    std::array<uint8_t, 256> log{};
    std::vector<uint8_t> product; // product[a * 256 + b]

    Field()
        : product(std::size_t{256} * 256)
    {
        unsigned x = 1;
        for (unsigned i = 0; i < 255; ++i)
        {
            exp[i] = static_cast<uint8_t>(x);
            log[x] = static_cast<uint8_t>(i);
            x <<= 1U;
            if ((x & 0x100U) != 0)
            {
                x ^= 0x11DU;
            }
        }
        for (unsigned i = 255; i < exp.size(); ++i)
        {
            exp[i] = exp[i - 255];
        }
        for (unsigned a = 1; a < 256; ++a)
        {
            for (unsigned b = 1; b < 256; ++b)
            {
                product[(a * 256) + b] = exp[unsigned{log[a]} + log[b]];
            }
        }
    }

    uint8_t multiply(uint8_t a, uint8_t b) const
    {
        return product[(std::size_t{a} * 256) + b];
    }

    uint8_t inverse(uint8_t a) const
    {
        return exp[255 - unsigned{log[a]}];
    }
};

const Field& field()
{
    static const Field built;
    return built;
}

// Row `row` of the generator matrix: the unit row for a data shard, the Cauchy row 1 / (x + y)
// with x = k + p and y = i for parity shard p.
std::vector<uint8_t> generator_row(const RepairLayout& layout, unsigned row)
{
    std::vector<uint8_t> coefficients(layout.data_shards);
    if (row < layout.data_shards)
    {
        coefficients[row] = 1;
        return coefficients;
    }
    const unsigned x = row;
    for (unsigned i = 0; i < layout.data_shards; ++i)
    {
        coefficients[i] = field().inverse(static_cast<uint8_t>(x ^ i));
    }
    return coefficients;
}

// out ^= coefficient * in, byte by byte.
void multiply_add(uint8_t coefficient, std::span<const uint8_t> in, std::span<uint8_t> out)
{
    if (coefficient == 0)
    {
        return;
    }
    const uint8_t* row = field().product.data() + (std::size_t{coefficient} * 256);
    for (std::size_t b = 0; b < in.size(); ++b)
    {
        out[b] ^= row[in[b]];
    }
}

void put_le(std::vector<uint8_t>& out, uint64_t value, std::size_t bytes)
{
    for (std::size_t i = 0; i < bytes; ++i)
    {
        out.push_back(static_cast<uint8_t>(value >> (8 * i)));
    }
}

uint64_t get_le(std::span<const uint8_t> bytes)
{
    uint64_t value = 0;
    for (std::size_t i = 0; i < bytes.size(); ++i)
    {
        value |= uint64_t{bytes[i]} << (8 * i);
    }
    return value;
}

std::size_t header_bytes(const RepairLayout& layout)
{
    return fixed_header_bytes + (4 * std::size_t{layout.data_shards + layout.parity_shards}) + 4;
}

// `count` bytes of `in` at `at`, zeros past its end.
void read_at(std::istream& in, uint64_t at, std::span<uint8_t> bytes)
{
    std::ranges::fill(bytes, 0);
    in.clear();
    in.seekg(static_cast<std::streamoff>(at));
    if (in)
    {
        in.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }
    in.clear();
}

struct Header
{
    RepairLayout layout;
    std::vector<uint32_t> crcs; // data shards, then parity shards
};

std::expected<Header, Error> read_header(std::istream& repair)
{
    std::vector<uint8_t> fixed(fixed_header_bytes);
    repair.clear();
    repair.seekg(0);
    repair.read(reinterpret_cast<char*>(fixed.data()), static_cast<std::streamsize>(fixed.size()));
    if (!repair || !std::equal(magic.begin(), magic.end(), fixed.begin()) || fixed[4] != version)
    {
        return std::unexpected(Error::corrupt_header);
    }
    Header header;
    header.layout = {.file_size = get_le(std::span(fixed).subspan(5, 8)),
                     .shard_size = get_le(std::span(fixed).subspan(13, 8)),
                     .data_shards = fixed[21],
                     .parity_shards = fixed[22]};
    const RepairLayout& layout = header.layout;
    if (layout.data_shards == 0 || layout.parity_shards == 0 ||
        layout.data_shards + layout.parity_shards > max_shards || layout.shard_size == 0 ||
        layout.file_size > layout.shard_size * layout.data_shards ||
        layout.file_size <= layout.shard_size * (layout.data_shards - 1))
    {
        return std::unexpected(Error::corrupt_header);
    }
    std::vector<uint8_t> rest(header_bytes(layout) - fixed_header_bytes);
    repair.read(reinterpret_cast<char*>(rest.data()), static_cast<std::streamsize>(rest.size()));
    if (!repair)
    {
        return std::unexpected(Error::corrupt_header);
    }
    std::vector<uint8_t> all = fixed;
    all.insert(all.end(), rest.begin(), rest.end() - 4);
    if (crc32(all) != static_cast<uint32_t>(get_le(std::span(rest).last(4))))
    {
        return std::unexpected(Error::corrupt_header);
    }
    for (unsigned s = 0; s < layout.data_shards + layout.parity_shards; ++s)
    {
        header.crcs.push_back(
            static_cast<uint32_t>(get_le(std::span(rest).subspan(std::size_t{4} * s, 4))));
    }
    return header;
}

// Shard `s`'s bytes at `offset`: a data shard from the file, a parity shard from the repair file.
void read_shard(std::istream& file, std::istream& repair, const RepairLayout& layout, unsigned s,
                uint64_t offset, std::span<uint8_t> bytes)
{
    if (s < layout.data_shards)
    {
        const uint64_t at = (s * layout.shard_size) + offset;
        read_at(file, at, bytes);
        // The padding after the file's end is zero, whatever the file has there.
        if (at + bytes.size() > layout.file_size)
        {
            const uint64_t keep = at < layout.file_size ? layout.file_size - at : 0;
            std::fill(bytes.begin() + static_cast<std::ptrdiff_t>(keep), bytes.end(), 0);
        }
        return;
    }
    read_at(repair, header_bytes(layout) + ((s - layout.data_shards) * layout.shard_size) + offset,
            bytes);
}

std::expected<RepairCheck, Error> check(std::istream& file, std::istream& repair,
                                        const Header& header)
{
    const RepairLayout& layout = header.layout;
    RepairCheck result{
        .layout = layout, .damaged_data = {}, .damaged_parity = {}, .size_matches = true};
    file.clear();
    file.seekg(0, std::ios::end);
    const std::streamoff end = file.tellg();
    result.size_matches = std::cmp_equal(end, layout.file_size);
    std::vector<uint8_t> bytes(stripe);
    for (unsigned s = 0; s < layout.data_shards + layout.parity_shards; ++s)
    {
        uint32_t crc = 0;
        for (uint64_t offset = 0; offset < layout.shard_size; offset += stripe)
        {
            const auto count =
                static_cast<std::size_t>(std::min<uint64_t>(stripe, layout.shard_size - offset));
            read_shard(file, repair, layout, s, offset, std::span(bytes).first(count));
            crc = crc32(std::span(bytes).first(count), crc);
        }
        if (crc != header.crcs[s])
        {
            (s < layout.data_shards ? result.damaged_data : result.damaged_parity)
                .push_back(s < layout.data_shards ? s : s - layout.data_shards);
        }
    }
    return result;
}

// The inverse of a k x k matrix over GF(2^8), or nothing when it is singular.
std::vector<std::vector<uint8_t>> invert(std::vector<std::vector<uint8_t>> a)
{
    const std::size_t n = a.size();
    std::vector<std::vector<uint8_t>> inverse(n, std::vector<uint8_t>(n));
    for (std::size_t i = 0; i < n; ++i)
    {
        inverse[i][i] = 1;
    }
    for (std::size_t column = 0; column < n; ++column)
    {
        std::size_t pivot = column;
        while (pivot < n && a[pivot][column] == 0)
        {
            ++pivot;
        }
        if (pivot == n)
        {
            return {};
        }
        std::swap(a[pivot], a[column]);
        std::swap(inverse[pivot], inverse[column]);
        const uint8_t scale = field().inverse(a[column][column]);
        for (std::size_t j = 0; j < n; ++j)
        {
            a[column][j] = field().multiply(a[column][j], scale);
            inverse[column][j] = field().multiply(inverse[column][j], scale);
        }
        for (std::size_t row = 0; row < n; ++row)
        {
            const uint8_t factor = a[row][column];
            if (row == column || factor == 0)
            {
                continue;
            }
            for (std::size_t j = 0; j < n; ++j)
            {
                a[row][j] ^= field().multiply(factor, a[column][j]);
                inverse[row][j] ^= field().multiply(factor, inverse[column][j]);
            }
        }
    }
    return inverse;
}

} // namespace

RepairLayout repair_layout(uint64_t file_size, unsigned percent)
{
    const uint64_t size = std::max<uint64_t>(file_size, 1);
    const auto k = static_cast<unsigned>(
        std::clamp<uint64_t>((size + min_shard_size - 1) / min_shard_size, 1, max_data_shards));
    const uint64_t shard = (size + k - 1) / k;
    const auto m = std::clamp((k * std::max(percent, 1U) + 99) / 100, 1U, max_shards - k);
    // Recount k for that shard size, so the last shard isn't empty.
    const auto used = static_cast<unsigned>((size + shard - 1) / shard);
    return {.file_size = file_size, .shard_size = shard, .data_shards = used, .parity_shards = m};
}

bool RepairCheck::intact() const
{
    return damaged_data.empty() && damaged_parity.empty() && size_matches;
}

bool RepairCheck::recoverable() const
{
    return damaged_data.size() + damaged_parity.size() <= layout.parity_shards;
}

std::expected<void, Error> write_repair(std::istream& file, uint64_t file_size, unsigned percent,
                                        std::ostream& out)
{
    const RepairLayout layout = repair_layout(file_size, percent);
    const unsigned k = layout.data_shards;
    const unsigned m = layout.parity_shards;
    std::vector<std::vector<uint8_t>> rows;
    rows.reserve(m);
    for (unsigned p = 0; p < m; ++p)
    {
        rows.push_back(generator_row(layout, k + p));
    }
    std::vector<uint32_t> crcs(k + m, 0);
    const std::size_t header = header_bytes(layout);
    // The header's CRCs are known only at the end; the parity shards go after its space.
    const std::vector<char> zeros(header, 0);
    out.write(zeros.data(), static_cast<std::streamsize>(zeros.size()));
    std::vector<uint8_t> data(stripe);
    std::vector<std::vector<uint8_t>> parity(m, std::vector<uint8_t>(stripe));
    for (uint64_t offset = 0; offset < layout.shard_size; offset += stripe)
    {
        const auto count =
            static_cast<std::size_t>(std::min<uint64_t>(stripe, layout.shard_size - offset));
        for (auto& p : parity)
        {
            std::fill_n(p.begin(), count, 0);
        }
        for (unsigned i = 0; i < k; ++i)
        {
            const std::span<uint8_t> bytes = std::span(data).first(count);
            read_shard(file, file, layout, i, offset, bytes);
            crcs[i] = crc32(bytes, crcs[i]);
            for (unsigned p = 0; p < m; ++p)
            {
                multiply_add(rows[p][i], bytes, std::span(parity[p]).first(count));
            }
        }
        for (unsigned p = 0; p < m; ++p)
        {
            crcs[k + p] = crc32(std::span(parity[p]).first(count), crcs[k + p]);
            out.seekp(static_cast<std::streamoff>(header + (p * layout.shard_size) + offset));
            out.write(reinterpret_cast<const char*>(parity[p].data()),
                      static_cast<std::streamsize>(count));
        }
    }
    std::vector<uint8_t> fields(magic.begin(), magic.end());
    fields.push_back(version);
    put_le(fields, layout.file_size, 8);
    put_le(fields, layout.shard_size, 8);
    fields.push_back(static_cast<uint8_t>(k));
    fields.push_back(static_cast<uint8_t>(m));
    for (const uint32_t crc : crcs)
    {
        put_le(fields, crc, 4);
    }
    put_le(fields, crc32(fields), 4);
    out.seekp(0);
    out.write(reinterpret_cast<const char*>(fields.data()),
              static_cast<std::streamsize>(fields.size()));
    out.seekp(0, std::ios::end);
    out.flush();
    if (!out)
    {
        return std::unexpected(Error::write_failed);
    }
    return {};
}

std::expected<RepairCheck, Error> check_repair(std::istream& file, std::istream& repair)
{
    const auto header = read_header(repair);
    if (!header)
    {
        return std::unexpected(header.error());
    }
    return check(file, repair, *header);
}

std::expected<RepairCheck, Error> repair_file(std::istream& file, std::istream& repair,
                                              std::ostream& out)
{
    const auto header = read_header(repair);
    if (!header)
    {
        return std::unexpected(header.error());
    }
    auto checked = check(file, repair, *header);
    if (!checked)
    {
        return checked;
    }
    const RepairLayout& layout = header->layout;
    if (!checked->recoverable())
    {
        return std::unexpected(Error::corrupt_payload);
    }
    const unsigned k = layout.data_shards;
    // k intact shards, data shards first, and the matrix that rebuilds the data from them.
    std::vector<unsigned> good;
    std::vector<bool> damaged(k + layout.parity_shards, false);
    for (const unsigned d : checked->damaged_data)
    {
        damaged[d] = true;
    }
    for (const unsigned p : checked->damaged_parity)
    {
        damaged[k + p] = true;
    }
    for (unsigned s = 0; s < damaged.size() && good.size() < k; ++s)
    {
        if (!damaged[s])
        {
            good.push_back(s);
        }
    }
    std::vector<std::vector<uint8_t>> rows;
    rows.reserve(k);
    for (const unsigned s : good)
    {
        rows.push_back(generator_row(layout, s));
    }
    const auto rebuild = invert(rows);
    if (rebuild.empty())
    {
        return std::unexpected(Error::corrupt_payload);
    }
    std::vector<std::vector<uint8_t>> inputs(k, std::vector<uint8_t>(stripe));
    std::vector<uint8_t> shard(stripe);
    for (uint64_t offset = 0; offset < layout.shard_size; offset += stripe)
    {
        const auto count =
            static_cast<std::size_t>(std::min<uint64_t>(stripe, layout.shard_size - offset));
        for (unsigned j = 0; j < k; ++j)
        {
            read_shard(file, repair, layout, good[j], offset, std::span(inputs[j]).first(count));
        }
        for (unsigned d = 0; d < k; ++d)
        {
            const uint64_t at = (d * layout.shard_size) + offset;
            if (at >= layout.file_size)
            {
                break;
            }
            std::fill_n(shard.begin(), count, 0);
            for (unsigned j = 0; j < k; ++j)
            {
                multiply_add(rebuild[d][j], std::span(inputs[j]).first(count),
                             std::span(shard).first(count));
            }
            const uint64_t keep = std::min<uint64_t>(count, layout.file_size - at);
            out.seekp(static_cast<std::streamoff>(at));
            out.write(reinterpret_cast<const char*>(shard.data()),
                      static_cast<std::streamsize>(keep));
        }
    }
    out.flush();
    if (!out)
    {
        return std::unexpected(Error::write_failed);
    }
    return checked;
}

} // namespace compressor
