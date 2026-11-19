#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <compressor/bitset/bitset.h>
#include <compressor/data/data.h>
#include <compressor/engine/engine.h>
#include <compressor/huffman/huffman.h>
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
            leaf->setTag(std::make_shared<bitset>(
                code_string({.bits = bits, .length = static_cast<uint8_t>(length)})));
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
    node->setTag(std::make_shared<bitset>(
        code_string({.bits = bits, .length = static_cast<uint8_t>(length)})));
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

constexpr std::array<uint8_t, 256> reversed_bytes = [] {
    std::array<uint8_t, 256> table{};
    for (std::size_t byte = 0; byte < table.size(); ++byte)
    {
        uint8_t reversed = 0;
        for (std::size_t bit = 0; bit < 8; ++bit)
        {
            if ((byte & (std::size_t{1} << bit)) != 0)
            {
                reversed = static_cast<uint8_t>(reversed | (0x80U >> bit));
            }
        }
        table[byte] = reversed;
    }
    return table;
}();

// Reads a bitset's bits in order, most significant first in a 64-bit buffer. The bitset stores
// each byte's first bit in its least significant bit, hence reversed_bytes. Past the end it
// reads zeros; remaining() tells how many real bits are left.
class BitReader
{
public:
    BitReader(std::span<const uint8_t> bytes, std::size_t bit_count)
        : bytes_(bytes)
        , bit_count_(bit_count)
    {
    }

    uint32_t peek()
    {
        while (buffered_ <= 56)
        {
            const uint64_t byte = next_ < bytes_.size() ? reversed_bytes[bytes_[next_]] : 0;
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

bitset to_bitset(const Code& code)
{
    bitset bits(0);
    for (std::size_t i = code.length; i-- > 0;)
    {
        bits.push_back(((code.bits >> i) & 1U) != 0);
    }
    return bits;
}

} // namespace

void Engine::set_codes(const CodeLengths& lengths)
{
    codes_ = canonical_codes(lengths);
    for (std::size_t symbol = 0; symbol < symbol_count; ++symbol)
    {
        code_bits_[symbol] = to_bitset(codes_[symbol]);
    }
}

void Engine::print_graph() const
{
    auto root = build_subtree(codes_, frequencies_, 0, 0);
    std::cout << "digraph G {\n";
    if (root)
    {
        BinaryTree tree;
        tree.setRoot(root);
        std::for_each(tree.preOrderBegin(), tree.preOrderEnd(),
                      [](const std::shared_ptr<BinaryNode>& bin_node) {
            auto node = std::static_pointer_cast<SymbolNode>(bin_node);
            const std::string id = "n" + node->tag()->str();
            std::cout << "\t\"" << id << "\" [label=\"" << node->value() << " ["
                      << node->frequency() << ", " << node->tag()->str() << "]\"];\n";
            for (const auto& child : {node->left(), node->right()})
            {
                if (child)
                {
                    std::cout << "\t\"" << id << "\" -> \"n" << child->tag()->str() << "\";\n";
                }
            }
        });
    }
    std::cout << "}\n";
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
    frequencies_ = {};
    {
        PROFILE_SCOPE("Count frequencies")
        for (const uint8_t symbol : data.data_)
        {
            ++frequencies_[symbol];
        }
    }

    EncodedData encoded;
    encoded.size_ = data.data_.size();
    {
        PROFILE_SCOPE("Code lengths")
        encoded.code_lengths_ = code_lengths(frequencies_);
        set_codes(encoded.code_lengths_);
    }

    {
        PROFILE_SCOPE("Encode symbols")
        bitset payload(0);
        for (const uint8_t symbol : data.data_)
        {
            payload.append(code_bits_[symbol]);
        }
        encoded.data_ = std::move(payload);
    }
    return encoded;
}

DecodedData Engine::decode(const EncodedData& data)
{
    frequencies_ = {};
    set_codes(data.code_lengths_);

    DecodedData result;
    if (data.size_ == 0)
    {
        return result;
    }
    // Every symbol takes at least one bit, so a corrupt size can't make this reserve too much.
    result.data_.reserve(std::min<std::size_t>(data.size_, data.data_.size()));

    const DecodeTable table = build_decode_table(codes_);
    BitReader reader(data.data_.data(), data.data_.size());
    for (uint64_t decoded = 0; decoded < data.size_; ++decoded)
    {
        const DecodeEntry entry = table[reader.peek()];
        if (entry.length == 0 || entry.length > reader.remaining())
        {
            return {};
        }
        result.data_.push_back(entry.symbol);
        reader.consume(entry.length);
    }
    return result;
}

} // namespace compressor
