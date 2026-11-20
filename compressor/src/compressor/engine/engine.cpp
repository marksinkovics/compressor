#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <iostream>
#include <memory>
#include <optional>
#include <ostream>
#include <ranges>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <compressor/bwt/bwt.h>
#include <compressor/checksum/crc32.h>
#include <compressor/data/data.h>
#include <compressor/deflate/deflate.h>
#include <compressor/deflate/inflate.h>
#include <compressor/engine/engine.h>
#include <compressor/entropy/rans.h>
#include <compressor/error.h>
#include <compressor/huffman/huffman.h>
#include <compressor/lz77/lz77.h>
#include <compressor/lzr/lzr.h>
#include <compressor/transform/transform.h>
#include <compressor/tree/binary/nodeiterator.h>
#include <compressor/tree/binary/tree.h>
#include <compressor/tree/symbolnode.h>

#include <instrumentor/instrumentor.h>

namespace compressor
{

namespace
{

std::string code_string(const Code& code)
{
    std::string text;
    for (std::size_t i = code.length; i-- > 0;)
    {
        text += ((code.bits >> i) & 1U) != 0 ? '1' : '0';
    }
    return text;
}

std::string symbol_label(uint8_t symbol)
{
    if (symbol >= 0x20 && symbol < 0x7F && symbol != '"' && symbol != '\\')
    {
        return {static_cast<char>(symbol)};
    }
    constexpr const char* hex = "0123456789ABCDEF";
    return std::string("0x") + hex[symbol >> 4U] + hex[symbol & 0x0FU];
}

// The subtree below the code prefix `bits` of length `length`, or null if no code starts with it.
std::shared_ptr<SymbolNode> build_subtree(const Codes& codes, const Frequencies& frequencies,
                                          uint32_t bits, std::size_t length)
{
    for (std::size_t symbol = 0; length > 0 && symbol < symbol_count; ++symbol)
    {
        if (codes[symbol].length == length && codes[symbol].bits == bits)
        {
            auto leaf = std::make_shared<SymbolNode>(symbol_label(static_cast<uint8_t>(symbol)),
                                                     frequencies[symbol]);
            leaf->setTag(code_string({.bits = bits, .length = static_cast<uint8_t>(length)}));
            return leaf;
        }
    }
    if (length >= max_code_length)
    {
        return nullptr;
    }

    auto left = build_subtree(codes, frequencies, bits << 1U, length + 1);
    auto right = build_subtree(codes, frequencies, (bits << 1U) | 1U, length + 1);
    if (!left && !right)
    {
        return nullptr;
    }
    const std::string value = (left ? left->value() : "") + (right ? right->value() : "");
    const uint64_t frequency = (left ? left->frequency() : 0) + (right ? right->frequency() : 0);
    auto node = std::make_shared<SymbolNode>(value, frequency);
    node->setTag(code_string({.bits = bits, .length = static_cast<uint8_t>(length)}));
    // The pre-order iterator walks back up through the parents.
    if (left)
    {
        left->setParent(node);
        node->setLeft(left);
    }
    if (right)
    {
        right->setParent(node);
        node->setRight(right);
    }
    return node;
}

// Past the end it reads zeros; remaining() tells how many real bits are left.
class BitReader
{
public:
    explicit BitReader(std::span<const uint8_t> bytes)
        : bytes_(bytes)
        , bit_count_(bytes.size() * 8)
    {
    }

    uint32_t peek()
    {
        while (buffered_ <= 56)
        {
            const uint64_t byte = next_ < bytes_.size() ? bytes_[next_] : 0;
            ++next_;
            buffer_ |= byte << (56 - buffered_);
            buffered_ += 8;
        }
        return static_cast<uint32_t>(buffer_ >> (64 - max_code_length));
    }

    // Only after peek(), with length <= max_code_length.
    void consume(std::size_t length)
    {
        buffer_ <<= length;
        buffered_ -= length;
        position_ += length;
    }

    std::size_t remaining() const
    {
        return bit_count_ - position_;
    }

private:
    std::span<const uint8_t> bytes_;
    std::size_t bit_count_;
    std::size_t next_ = 0;
    std::size_t position_ = 0;
    uint64_t buffer_ = 0;
    std::size_t buffered_ = 0;
};

// Writes 32 bits at a time: with codes of at most 15 bits, the buffer never holds more than 46.
// `out` must have room for every code put, rounded up to a whole byte.
class BitWriter
{
public:
    explicit BitWriter(std::span<uint8_t> out)
        : out_(out)
    {
    }

    void put(const Code& code)
    {
        buffer_ = (buffer_ << code.length) | code.bits;
        buffered_ += code.length;
        if (buffered_ >= 32)
        {
            buffered_ -= 32;
            const auto word = static_cast<uint32_t>(buffer_ >> buffered_);
            out_[next_++] = static_cast<uint8_t>(word >> 24U);
            out_[next_++] = static_cast<uint8_t>(word >> 16U);
            out_[next_++] = static_cast<uint8_t>(word >> 8U);
            out_[next_++] = static_cast<uint8_t>(word);
        }
    }

    // Pads the last byte with zeros.
    void flush()
    {
        while (buffered_ >= 8)
        {
            buffered_ -= 8;
            out_[next_++] = static_cast<uint8_t>(buffer_ >> buffered_);
        }
        if (buffered_ > 0)
        {
            out_[next_++] = static_cast<uint8_t>(buffer_ << (8 - buffered_));
            buffered_ = 0;
        }
    }

private:
    std::span<uint8_t> out_;
    std::size_t next_ = 0;
    uint64_t buffer_ = 0;
    std::size_t buffered_ = 0;
};

} // namespace

std::vector<std::unique_ptr<IEngine>> make_engines(const EngineFactory& make_engine, unsigned count)
{
    std::vector<std::unique_ptr<IEngine>> engines(std::max(1U, count));
    for (auto& engine : engines)
    {
        engine = make_engine();
    }
    return engines;
}

Engine::Engine(uint8_t block_size_log2, unsigned level)
    : block_size_log2_(block_size_log2)
    , level_(level)
{
}

void Engine::set_codes(const CodeLengths& lengths)
{
    codes_ = canonical_codes(lengths);
}

void write_code_graph(std::ostream& out, const Codes& codes, const Frequencies& frequencies)
{
    auto root = build_subtree(codes, frequencies, 0, 0);
    out << "digraph G {\n";
    if (root)
    {
        BinaryTree tree;
        tree.setRoot(root);
        std::for_each(tree.preOrderBegin(), tree.preOrderEnd(),
                      [&out](const std::shared_ptr<BinaryNode>& bin_node) {
            auto node = std::static_pointer_cast<SymbolNode>(bin_node);
            const std::string id = "n" + node->tag();
            out << "\t\"" << id << "\" [label=\"" << node->value() << " [" << node->frequency()
                << ", " << node->tag() << "]\"];\n";
            for (const auto& child : {node->left(), node->right()})
            {
                if (child)
                {
                    out << "\t\"" << id << "\" -> \"n" << child->tag() << "\";\n";
                }
            }
        });
    }
    out << "}\n";
}

void Engine::print_graph() const
{
    write_code_graph(std::cout, codes_, frequencies_);
}

void Engine::print_dict() const
{
    for (std::size_t symbol = 0; symbol < symbol_count; ++symbol)
    {
        if (codes_[symbol].length > 0)
        {
            std::cout << code_string(codes_[symbol]) << " => ["
                      << symbol_label(static_cast<uint8_t>(symbol)) << "]\n";
        }
    }
}

EncodedData Engine::encode(const DecodedData& data)
{
    EncodedData encoded;
    encoded.block_size_log2_ = block_size_log2_;
    const std::size_t block_size = std::size_t{1} << block_size_log2_;
    const std::span<const uint8_t> bytes = data.data_;
    for (std::size_t offset = 0; offset < bytes.size(); offset += block_size)
    {
        encoded.blocks_.push_back(
            encode_block(bytes.subspan(offset, std::min(block_size, bytes.size() - offset))));
    }
    if (bytes.empty())
    {
        frequencies_ = {};
        codes_ = {};
    }
    return encoded;
}

std::expected<DecodedData, Error> Engine::decode(const EncodedData& data)
{
    frequencies_ = {};
    codes_ = {};
    DecodedData result;
    std::size_t total = 0;
    for (const EncodedBlock& block : data.blocks_)
    {
        total += block.size_;
    }
    result.data_.reserve(total);
    for (const EncodedBlock& block : data.blocks_)
    {
        if (auto decoded = decode_block(block, result.data_); !decoded)
        {
            return std::unexpected(decoded.error());
        }
    }
    return result;
}

EncodedBlock Engine::encode_block(std::span<const uint8_t> bytes)
{
    frequencies_ = {};
    {
        PROFILE_SCOPE("Count frequencies")
        for (const uint8_t symbol : bytes)
        {
            ++frequencies_[symbol];
        }
    }

    EncodedBlock encoded;
    encoded.size_ = static_cast<uint32_t>(bytes.size());
    {
        PROFILE_SCOPE("Checksum")
        encoded.crc_ = crc32(bytes);
    }
    CodeLengths lengths{};
    {
        PROFILE_SCOPE("Code lengths")
        lengths = code_lengths(frequencies_);
        set_codes(lengths);
    }

    uint64_t bit_count = 0;
    for (std::size_t symbol = 0; symbol < symbol_count; ++symbol)
    {
        bit_count += frequencies_[symbol] * codes_[symbol].length;
    }
    const uint64_t payload_bytes = (bit_count + 7) / 8;
    const uint64_t best_bytes =
        std::min<uint64_t>(bytes.size(), code_lengths_bytes + payload_bytes);
    if (level_ > 1)
    {
        PROFILE_SCOPE("Pipeline")
        const auto pipeline = [&](std::span<const Transform> transforms, Coder coder) {
            std::span<const uint8_t> input = bytes;
            std::vector<uint8_t> transformed;
            if (!transforms.empty())
            {
                transformed.assign(bytes.begin(), bytes.end());
                for (const Transform& transform : transforms)
                {
                    apply_transform(transform, transformed);
                }
                input = transformed;
            }
            std::vector<uint8_t> coded;
            switch (coder)
            {
            case Coder::lz77_huffman:
                coded = deflate_bytes(input, lz77_options_for_level(level_));
                break;
            case Coder::bwt:
                coded = bwt_encode(input);
                break;
            case Coder::rans:
                rans_encode(std::vector<uint16_t>(input.begin(), input.end()), 256, coded);
                break;
            case Coder::lzr:
                // Its 1 MiB window makes long chains slow; level 8 searches like level 7.
                coded = lzr_encode(
                    input, lz77_options_for_level(level_ >= max_level ? level_ : auto_level));
                break;
            }
            std::vector<uint8_t> payload{static_cast<uint8_t>(transforms.size())};
            for (const Transform& transform : transforms)
            {
                payload.push_back(static_cast<uint8_t>(transform.id));
                for (unsigned shift = 0; shift < 32; shift += 8)
                {
                    payload.push_back(static_cast<uint8_t>(transform.parameter >> shift));
                }
            }
            payload.push_back(static_cast<uint8_t>(coder));
            payload.insert(payload.end(), coded.begin(), coded.end());
            return payload;
        };
        // A transform must beat coding the block without it; a tie keeps none, and LZ77.
        std::vector<Coder> coders{Coder::lz77_huffman};
        if (level_ >= bwt_level)
        {
            coders.push_back(Coder::bwt);
            coders.push_back(Coder::lzr);
        }
        const std::vector<Transform> transforms = choose_transforms(bytes, level_ >= auto_level);
        std::vector<uint8_t> best_pipeline = pipeline({}, Coder::lz77_huffman);
        for (const Coder coder : coders)
        {
            if (coder != Coder::lz77_huffman)
            {
                std::vector<uint8_t> candidate = pipeline({}, coder);
                if (candidate.size() < best_pipeline.size())
                {
                    best_pipeline = std::move(candidate);
                }
            }
            for (const Transform& transform : transforms)
            {
                std::vector<uint8_t> candidate = pipeline(std::span(&transform, 1), coder);
                if (candidate.size() < best_pipeline.size())
                {
                    best_pipeline = std::move(candidate);
                }
            }
        }
        // Order-0 rANS only wins on transformed noisy samples (delta'd images), so it is only
        // tried after a transform.
        for (const Transform& transform : transforms)
        {
            std::vector<uint8_t> candidate = pipeline(std::span(&transform, 1), Coder::rans);
            if (candidate.size() < best_pipeline.size())
            {
                best_pipeline = std::move(candidate);
            }
        }
        // Only when strictly smaller: Huffman and stored decode faster.
        if (best_pipeline.size() < best_bytes)
        {
            codes_ = {};
            encoded.method_ = Method::pipeline;
            encoded.payload_ = std::move(best_pipeline);
            return encoded;
        }
    }
    // Ties go to stored, which decodes faster.
    if (bytes.size() <= code_lengths_bytes + payload_bytes)
    {
        codes_ = {};
        encoded.method_ = Method::stored;
        encoded.payload_.assign(bytes.begin(), bytes.end());
        return encoded;
    }

    {
        PROFILE_SCOPE("Encode symbols")
        encoded.code_lengths_ = lengths;
        encoded.payload_.resize(payload_bytes);
        BitWriter writer(encoded.payload_);
        for (const uint8_t symbol : bytes)
        {
            writer.put(codes_[symbol]);
        }
        writer.flush();
    }
    return encoded;
}

std::expected<void, Error> Engine::decode_block(const EncodedBlock& block,
                                                std::vector<uint8_t>& out)
{
    frequencies_ = {};
    codes_ = {};
    const std::size_t start = out.size();
    if (block.method_ == Method::stored)
    {
        if (block.payload_.size() != block.size_)
        {
            return std::unexpected(Error::corrupt_payload);
        }
        out.insert(out.end(), block.payload_.begin(), block.payload_.end());
    }
    else if (block.method_ == Method::pipeline)
    {
        if (auto decoded = decode_pipeline(block, out); !decoded)
        {
            out.resize(start);
            return decoded;
        }
    }
    else if (auto decoded = decode_huffman(block, out); !decoded)
    {
        out.resize(start);
        return decoded;
    }

    if (crc32(std::span(out).subspan(start)) != block.crc_)
    {
        out.resize(start);
        return std::unexpected(Error::checksum_mismatch);
    }
    return {};
}

std::expected<void, Error> Engine::decode_pipeline(const EncodedBlock& block,
                                                   std::vector<uint8_t>& out)
{
    std::span<const uint8_t> payload = block.payload_;
    if (payload.empty() || payload[0] > max_pipeline_transforms)
    {
        return std::unexpected(Error::corrupt_payload);
    }
    const std::size_t count = payload[0];
    if (payload.size() < pipeline_descriptor_bytes + (count * pipeline_transform_bytes))
    {
        return std::unexpected(Error::corrupt_payload);
    }
    std::vector<Transform> transforms;
    for (std::size_t i = 0; i < count; ++i)
    {
        const std::span<const uint8_t> field = payload.subspan(1 + (i * pipeline_transform_bytes));
        uint32_t parameter = 0;
        for (unsigned byte = 0; byte < 4; ++byte)
        {
            parameter |= uint32_t{field[1 + byte]} << (8 * byte);
        }
        const std::optional<Transform> transform = parse_transform(field[0], parameter);
        if (!transform)
        {
            return std::unexpected(Error::corrupt_payload);
        }
        transforms.push_back(*transform);
    }
    payload = payload.subspan(1 + (count * pipeline_transform_bytes));
    const std::size_t start = out.size();
    std::expected<void, Error> decoded = std::unexpected(Error::corrupt_payload);
    if (payload[0] == static_cast<uint8_t>(Coder::lz77_huffman))
    {
        decoded = inflate_bytes(payload.subspan(1), out, block.size_);
    }
    else if (payload[0] == static_cast<uint8_t>(Coder::bwt))
    {
        decoded = bwt_decode(payload.subspan(1), out, block.size_);
    }
    else if (payload[0] == static_cast<uint8_t>(Coder::lzr))
    {
        decoded = lzr_decode(payload.subspan(1), out, block.size_);
    }
    else if (payload[0] == static_cast<uint8_t>(Coder::rans))
    {
        auto symbols = rans_decode(payload.subspan(1), 256, block.size_);
        if (symbols)
        {
            out.insert(out.end(), symbols->begin(), symbols->end());
            decoded = {};
        }
    }
    if (!decoded)
    {
        out.resize(start);
        return decoded;
    }
    const std::span<uint8_t> restored = std::span(out).subspan(start);
    for (const Transform& transform : transforms | std::views::reverse)
    {
        undo_transform(transform, restored);
    }
    return {};
}

std::expected<void, Error> Engine::decode_huffman(const EncodedBlock& block,
                                                  std::vector<uint8_t>& out)
{
    set_codes(block.code_lengths_);
    if (block.size_ == 0)
    {
        if (!block.payload_.empty())
        {
            return std::unexpected(Error::corrupt_payload);
        }
        return {};
    }
    // Every symbol takes at least one bit, so a corrupt size can't make this reserve too much.
    out.reserve(out.size() + std::min<std::size_t>(block.size_, block.payload_.size() * 8));

    const DecodeTable table = build_decode_table(codes_);
    BitReader reader(block.payload_);
    for (uint32_t decoded = 0; decoded < block.size_; ++decoded)
    {
        const DecodeEntry entry = table[reader.peek()];
        if (entry.length == 0 || entry.length > reader.remaining())
        {
            return std::unexpected(Error::corrupt_payload);
        }
        out.push_back(entry.symbol);
        reader.consume(entry.length);
    }
    // peek() covers the rest of the last byte, and reads zeros past the payload.
    if (reader.remaining() >= 8 || reader.peek() != 0)
    {
        return std::unexpected(Error::corrupt_payload);
    }
    return {};
}

} // namespace compressor
