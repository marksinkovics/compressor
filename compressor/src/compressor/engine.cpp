#include <iostream>
#include <map>
#include <utility>
#include <vector>
#include <string>
#include <iomanip>

#include <compressor/bitset/bitset.h>
#include <compressor/bitset/bitset_iterator.h>
#include <compressor/tree/binary/tree.h>
#include <compressor/engine.h>
#include <compressor/tree/symbolnode.h>
#include <compressor/tree/binary/nodeiterator.h>

#include <compressor/data/data.h>

namespace compressor
{
    
Engine::Engine(const std::string& input)
{
    std::vector<uint8_t> input_data(input.begin(), input.end());
    this->data_.data_ = std::move(input_data);
}

Engine::Engine(const Data& data)
{
    this->data_ = data;
}

Engine::Engine(const EncodedData& data)
    : encoded_data_(data)
{
    
}

void Engine::build_dictionary() noexcept
{
    for (const char& symbol : data_.data_)
    {
        auto pos = _symbols_dict.find(symbol);
        if (pos != _symbols_dict.end())
        {
            ++(*pos->second);
            continue;
        }
        auto node = std::make_shared<SymbolNode>(std::string(1, symbol));
        auto pair = std::make_pair(symbol, node);
        _symbols_dict.insert(pair);
    }
}

void Engine::build_tree() noexcept
{
    _tree = std::make_shared<BinaryTree>();
    
    std::function<std::shared_ptr<SymbolNode>(std::pair<char, std::shared_ptr<SymbolNode>>)> second = [](std::pair<char, std::shared_ptr<SymbolNode>> const &pair) {
        return pair.second;
    };
    
    std::function<bool(std::shared_ptr<SymbolNode>,std::shared_ptr<SymbolNode>)> lower = [](std::shared_ptr<SymbolNode> const &lhs, std::shared_ptr<SymbolNode> const &rhs) {
        return lhs->frequency() < rhs->frequency();
    };
    
    std::vector<std::shared_ptr<SymbolNode>> _nodes;
    std::transform(_symbols_dict.begin(), _symbols_dict.end(), std::back_inserter(_nodes), second);
    
    while(_nodes.size() > 1)
    {
        std::sort(_nodes.begin(), _nodes.end(), lower);
        auto left = _nodes.front(), right = (*(++_nodes.begin()));
        auto parent = std::make_shared<SymbolNode>(left->value() + right->value(), left->frequency() + right->frequency());
        parent->setLeft(left);
        left->setParent(parent);
        parent->setRight(right);
        right->setParent(parent);
        _nodes.erase(_nodes.begin(), _nodes.begin() + 2);
        _nodes.push_back(parent);
    }
    
    _tree->setRoot(_nodes.front());
}

void Engine::create_hash_table() noexcept
{
    std::for_each(_tree->preOrderBegin(), _tree->preOrderEnd(), [](std::shared_ptr<BinaryNode> bin_node) {
        auto node = std::dynamic_pointer_cast<SymbolNode>(bin_node);
        
        if (!node->hasParent())
        {
            node->setTag(std::make_shared<compressor::bitset>(0));
            return;
        }
        
        bool isRightChild = (node->parent().lock()->right() == node);
        std::shared_ptr<compressor::bitset> parentVector = node->parent().lock()->tag();
        node->setTag(std::make_shared<bitset>(*parentVector));
        node->tag()->push_back(isRightChild);
    });
    
    for(const auto& pair : this->_symbols_dict)
    {
        auto node = std::dynamic_pointer_cast<SymbolNode>(pair.second);
        uint8_t c = node->value().c_str()[0];
        byte_dict_[c] = *(node->tag());
        bit_dict_[*(node->tag())] = c;
    }
}

std::shared_ptr<compressor::bitset> Engine::find_path(std::shared_ptr<SymbolNode>& node) noexcept
{
    std::shared_ptr<SymbolNode> innerNode = node;
    auto vector = std::make_shared<compressor::bitset>(0);
    while (!innerNode->parent().expired())
    {
        bool isRightChild = (node->parent().lock()->right() == node);
        vector->push_back(isRightChild);
        innerNode = std::dynamic_pointer_cast<SymbolNode>(innerNode->parent().lock());
    }
    return vector;
}

void Engine::print_graph() const noexcept
{
    std::cout << "digraph G {\n";
    std::for_each(_tree->preOrderBegin(), _tree->preOrderEnd(), [](std::shared_ptr<BinaryNode> bin_node) {
        auto node = std::dynamic_pointer_cast<SymbolNode>(bin_node);
        auto vector = node->tag();
        std::cout << "\"" << node->value() << "\""
        << "[label=\"" << node->value() << "["<< node->frequency() << ", "<< node->tag()->str() << "]" << "\"];\n";
        if (node->left())
        {
            auto leftNode = std::dynamic_pointer_cast<SymbolNode>(node->left());
            std::cout << "\t\"" << node->value() << "\" -> \"" << leftNode->value() << "\";\n";
        }
        
        if (node->right())
        {
            auto rightNode = std::dynamic_pointer_cast<SymbolNode>(node->right());
            std::cout << "\t\"" << node->value() << "\" -> \"" << rightNode->value() << "\";\n";
        }
    });
    std::cout << "}\n";
}

void Engine::print_dict() const noexcept
{
    for(const auto it: bit_dict_)
    {
        std::cout << it.first.str() << " => [";
        std::cout << it.second;
        std::cout << "]\n";
    }
}

EncodedData Engine::encode()
{
    this->encoded_data_ = EncodedData();
    
    build_dictionary();
    build_tree();
    create_hash_table();
    
    bitset vector(0);
    for (const char& symbol : data_.data_)
    {
        auto node = _symbols_dict[symbol];
        auto tag = node->tag();
        vector.append(*tag);
    }
    encoded_data_.data_ = std::move(vector);
    encoded_data_.bit_dict_ = bit_dict_;
    
    std::size_t original_size = data_.data_.size();
    std::size_t encoded_size = encoded_data_.data_.num_bytes();
    
    std::cout << "Encoded: " << original_size << " => " << encoded_size
    << " (" << (double(encoded_size) / double(original_size) * 100.0) << "%)\n";
    
    return encoded_data_;
}

Data Engine::decode()
{
    auto current = std::make_unique<bitset>();
    std::string str;
    for (const bool bit : encoded_data_.data_)
    {
        current->push_back(bit);
        auto search = encoded_data_.bit_dict_.find(*current);
        if (search != encoded_data_.bit_dict_.end()) {
            uint8_t c = search->second;
            str += c;
            current.reset(new bitset());
        }
    }
    std::vector<uint8_t> result_data(str.begin(), str.end());
    Data result;
    result.data_ = std::move(result_data);
    return result;
}
    
}
