#include <algorithm>
#include <cstddef>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <compressor/bitset/bitset.h>
#include <compressor/bitset/bitset_iterator.h>
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
    bit_dict_.clear();
    for (std::size_t symbol = 0; symbol < symbol_count; ++symbol)
    {
        code_bits_[symbol] = to_bitset(codes_[symbol]);
        if (codes_[symbol].length > 0)
        {
            bit_dict_[code_bits_[symbol]] = static_cast<uint8_t>(symbol);
        }
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

    bitset current(0);
    for (const bool bit : data.data_)
    {
        current.push_back(bit);
        auto search = bit_dict_.find(current);
        if (search != bit_dict_.end())
        {
            result.data_.push_back(search->second);
            if (result.data_.size() == data.size_)
            {
                return result;
            }
            current = bitset(0);
        }
        else if (current.size() >= max_code_length)
        {
            break;
        }
    }
    return {};
}

} // namespace compressor
