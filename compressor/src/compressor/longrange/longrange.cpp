#include <compressor/longrange/longrange.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <utility>
#include <vector>

#include <compressor/checksum/crc32.h>
#include <compressor/chunk/chunk.h>
#include <compressor/data/data.h>
#include <compressor/error.h>
#include <compressor/huffman/huffman.h>

namespace compressor
{

namespace
{

constexpr std::size_t chunk_average = 512;
// Shorter copies cost about as much as the literals they replace.
constexpr std::size_t min_reference = 32;

void put_varint(std::vector<uint8_t>& out, uint64_t value)
{
    while (value >= 0x80)
    {
        out.push_back(static_cast<uint8_t>((value & 0x7FU) | 0x80U));
        value >>= 7U;
    }
    out.push_back(static_cast<uint8_t>(value));
}

class Reader
{
public:
    explicit Reader(std::span<const uint8_t> bytes)
        : bytes_(bytes)
    {
    }

    std::expected<uint32_t, Error> varint()
    {
        uint64_t value = 0;
        for (unsigned shift = 0; shift < 35; shift += 7)
        {
            if (at_ == bytes_.size())
            {
                return std::unexpected(Error::corrupt_payload);
            }
            const uint8_t byte = bytes_[at_++];
            value |= uint64_t{byte & 0x7FU} << shift;
            if ((byte & 0x80U) == 0)
            {
                if (value > UINT32_MAX)
                {
                    break;
                }
                return static_cast<uint32_t>(value);
            }
        }
        return std::unexpected(Error::corrupt_payload);
    }

    std::expected<std::span<const uint8_t>, Error> take(std::size_t count)
    {
        if (count > bytes_.size() - at_)
        {
            return std::unexpected(Error::corrupt_payload);
        }
        const std::span<const uint8_t> taken = bytes_.subspan(at_, count);
        at_ += count;
        return taken;
    }

    std::span<const uint8_t> rest() const
    {
        return bytes_.subspan(at_);
    }

private:
    std::span<const uint8_t> bytes_;
    std::size_t at_ = 0;
};

} // namespace

uint64_t window_blocks(uint8_t block_size_log2, uint8_t window_log2)
{
    return uint64_t{1} << static_cast<unsigned>(window_log2 - block_size_log2);
}

std::vector<uint8_t> write_references(std::span<const Reference> references,
                                      const EncodedBlock& literals)
{
    std::vector<uint8_t> out;
    out.reserve((references.size() * 8) + literals.payload_.size() + code_lengths_bytes + 16);
    put_varint(out, references.size());
    for (const Reference& reference : references)
    {
        put_varint(out, reference.literals);
        put_varint(out, reference.back);
        put_varint(out, reference.offset);
        put_varint(out, reference.length);
    }
    out.push_back(static_cast<uint8_t>(literals.method_));
    put_varint(out, literals.size_);
    for (unsigned i = 0; i < 4; ++i)
    {
        out.push_back(static_cast<uint8_t>(literals.crc_ >> (8 * i)));
    }
    if (literals.method_ == Method::huffman)
    {
        for (std::size_t i = 0; i < code_lengths_bytes; ++i)
        {
            const auto low = static_cast<unsigned>(literals.code_lengths_[2 * i] & 0x0FU);
            const auto high = static_cast<unsigned>(literals.code_lengths_[(2 * i) + 1] & 0x0FU);
            out.push_back(static_cast<uint8_t>(low | (high << 4U)));
        }
    }
    out.insert(out.end(), literals.payload_.begin(), literals.payload_.end());
    return out;
}

std::expected<void, Error> read_references(std::span<const uint8_t> payload,
                                           std::vector<Reference>& references,
                                           EncodedBlock& literals)
{
    Reader reader(payload);
    const auto count = reader.varint();
    // Each reference takes at least 4 bytes.
    if (!count || *count == 0 || *count > payload.size() / 4)
    {
        return std::unexpected(Error::corrupt_payload);
    }
    references.resize(*count);
    for (Reference& reference : references)
    {
        const auto run = reader.varint();
        const auto back = reader.varint();
        const auto offset = reader.varint();
        const auto length = reader.varint();
        if (!run || !back || !offset || !length || *back == 0 || *length == 0)
        {
            return std::unexpected(Error::corrupt_payload);
        }
        reference = {.literals = *run, .back = *back, .offset = *offset, .length = *length};
    }
    const auto method = reader.take(1);
    const auto size = reader.varint();
    const auto crc = reader.take(4);
    if (!method || !size || !crc || (*method)[0] > static_cast<uint8_t>(Method::pipeline))
    {
        return std::unexpected(Error::corrupt_payload);
    }
    literals.method_ = static_cast<Method>((*method)[0]);
    literals.size_ = *size;
    literals.crc_ = 0;
    for (unsigned i = 0; i < 4; ++i)
    {
        literals.crc_ |= uint32_t{(*crc)[i]} << (8 * i);
    }
    literals.code_lengths_ = {};
    if (literals.method_ == Method::huffman)
    {
        const auto lengths = reader.take(code_lengths_bytes);
        if (!lengths)
        {
            return std::unexpected(lengths.error());
        }
        for (std::size_t i = 0; i < code_lengths_bytes; ++i)
        {
            literals.code_lengths_[2 * i] = (*lengths)[i] & 0x0FU;
            literals.code_lengths_[(2 * i) + 1] = (*lengths)[i] >> 4U;
        }
        if (!valid_code_lengths(literals.code_lengths_) || literals.code_lengths_ == CodeLengths{})
        {
            return std::unexpected(Error::corrupt_payload);
        }
    }
    literals.payload_.assign(reader.rest().begin(), reader.rest().end());
    return {};
}

std::expected<void, Error> rebuild_block(const EncodedBlock& block,
                                         std::span<const Reference> references,
                                         std::span<const uint8_t> literals,
                                         const EarlierLiterals& earlier, std::vector<uint8_t>& out)
{
    const std::size_t start = out.size();
    const auto fail = [&](Error error) {
        out.resize(start);
        return std::unexpected(error);
    };
    std::size_t used = 0;
    for (const Reference& reference : references)
    {
        if (reference.literals > literals.size() - used ||
            reference.length > block.size_ - (out.size() - start) - reference.literals)
        {
            return fail(Error::corrupt_payload);
        }
        out.insert(out.end(), literals.begin() + static_cast<std::ptrdiff_t>(used),
                   literals.begin() + static_cast<std::ptrdiff_t>(used + reference.literals));
        used += reference.literals;
        const auto target = earlier(reference.back);
        if (!target)
        {
            return fail(target.error());
        }
        if (reference.offset > target->size() ||
            reference.length > target->size() - reference.offset)
        {
            return fail(Error::corrupt_payload);
        }
        const auto from = target->subspan(reference.offset, reference.length);
        out.insert(out.end(), from.begin(), from.end());
    }
    out.insert(out.end(), literals.begin() + static_cast<std::ptrdiff_t>(used), literals.end());
    if (out.size() - start != block.size_)
    {
        return fail(Error::corrupt_payload);
    }
    if (crc32(std::span(out).subspan(start)) != block.crc_)
    {
        return fail(Error::checksum_mismatch);
    }
    return {};
}

LongRangeMatcher::LongRangeMatcher(uint64_t window_blocks)
    : window_blocks_(window_blocks)
{
}

LongRangeMatcher::Split LongRangeMatcher::next(std::span<const uint8_t> block)
{
    Split split;
    std::size_t pending = 0; // the first byte not yet a literal or copied
    std::size_t start = 0;
    for (const std::size_t end : chunk_ends(block, chunk_average))
    {
        const std::size_t from = std::max(start, pending);
        start = end;
        if (end <= pending)
        {
            continue;
        }
        const auto found = index_.find(chunk_hash(block.subspan(from, end - from)));
        if (found == index_.end() || found->second.length != end - from)
        {
            continue;
        }
        const Location at = found->second;
        const std::span<const uint8_t> target =
            literals_[static_cast<std::size_t>(at.block - (blocks_ - literals_.size()))];
        if (!std::ranges::equal(target.subspan(at.offset, at.length),
                                block.subspan(from, end - from)))
        {
            continue;
        }
        std::size_t first = from;
        std::size_t last = end;
        std::size_t target_first = at.offset;
        std::size_t target_last = at.offset + at.length;
        while (first > pending && target_first > 0 && target[target_first - 1] == block[first - 1])
        {
            --first;
            --target_first;
        }
        while (last < block.size() && target_last < target.size() &&
               target[target_last] == block[last])
        {
            ++last;
            ++target_last;
        }
        if (last - first < min_reference)
        {
            continue;
        }
        split.literals.insert(split.literals.end(),
                              block.begin() + static_cast<std::ptrdiff_t>(pending),
                              block.begin() + static_cast<std::ptrdiff_t>(first));
        split.references.push_back({.literals = static_cast<uint32_t>(first - pending),
                                    .back = static_cast<uint32_t>(blocks_ - at.block),
                                    .offset = static_cast<uint32_t>(target_first),
                                    .length = static_cast<uint32_t>(last - first)});
        pending = last;
    }
    if (split.references.empty())
    {
        remember(block);
    }
    else
    {
        split.literals.insert(split.literals.end(),
                              block.begin() + static_cast<std::ptrdiff_t>(pending), block.end());
        remember(split.literals);
    }
    return split;
}

void LongRangeMatcher::remember(std::span<const uint8_t> literals)
{
    if (literals_.size() == window_blocks_)
    {
        const uint64_t oldest = blocks_ - literals_.size();
        for (const uint64_t hash : hashes_.front())
        {
            if (const auto found = index_.find(hash);
                found != index_.end() && found->second.block == oldest)
            {
                index_.erase(found);
            }
        }
        literals_.pop_front();
        hashes_.pop_front();
    }
    std::vector<uint64_t>& hashes = hashes_.emplace_back();
    std::size_t start = 0;
    for (const std::size_t end : chunk_ends(literals, chunk_average))
    {
        const uint64_t hash = chunk_hash(literals.subspan(start, end - start));
        index_.insert_or_assign(hash, Location{.block = blocks_,
                                               .offset = static_cast<uint32_t>(start),
                                               .length = static_cast<uint32_t>(end - start)});
        hashes.push_back(hash);
        start = end;
    }
    literals_.emplace_back(literals.begin(), literals.end());
    ++blocks_;
}

LongRangeHistory::LongRangeHistory(uint64_t window_blocks)
    : window_blocks_(window_blocks)
{
}

std::expected<std::span<const uint8_t>, Error> LongRangeHistory::earlier(uint32_t back) const
{
    if (back == 0 || back > literals_.size())
    {
        return std::unexpected(Error::corrupt_payload);
    }
    return std::span<const uint8_t>(literals_[literals_.size() - back]);
}

void LongRangeHistory::add(std::vector<uint8_t> literals)
{
    if (literals_.size() == window_blocks_)
    {
        literals_.pop_front();
    }
    literals_.push_back(std::move(literals));
}

std::expected<void, Error> LongRangeHistory::decode(IEngine& engine, const EncodedBlock& block,
                                                    std::vector<uint8_t>& out)
{
    if (block.method_ != Method::references)
    {
        const std::size_t start = out.size();
        if (auto decoded = engine.decode_block(block, out); !decoded)
        {
            return decoded;
        }
        add({out.begin() + static_cast<std::ptrdiff_t>(start), out.end()});
        return {};
    }
    std::vector<Reference> references;
    EncodedBlock inner;
    if (auto read = read_references(block.payload_, references, inner); !read)
    {
        return read;
    }
    std::vector<uint8_t> literals;
    if (auto decoded = engine.decode_block(inner, literals); !decoded)
    {
        return decoded;
    }
    if (auto rebuilt = rebuild_block(block, references, literals,
                                     [this](uint32_t back) {
        return earlier(back);
    }, out);
        !rebuilt)
    {
        return rebuilt;
    }
    add(std::move(literals));
    return {};
}

} // namespace compressor
