#ifndef Engine_h
#define Engine_h

#include <memory>
#include <string>
#include <map>

#include <compressor/data/data.h>

namespace compressor
{
    
class BinaryTree;
class SymbolNode;
class bitset;
    
struct Data;
struct EncodedData;
    
class Engine
{
public:
    Engine(const std::string& input);
    
    Engine(const Data& data);
    Engine(const EncodedData& data);
    
    void build_dictionary() noexcept;
    void build_tree() noexcept;
    void create_hash_table() noexcept;
    
    std::shared_ptr<compressor::bitset> find_path(std::shared_ptr<SymbolNode>& node) noexcept;
    void print_graph() const noexcept;
    void print_dict() const noexcept;
    
    EncodedData encode();
    Data decode();
    
private:
    std::shared_ptr<BinaryTree> _tree;
    std::map<uint8_t, std::shared_ptr<SymbolNode>> _symbols_dict;
    
    std::map<uint8_t, compressor::bitset> byte_dict_;
    std::map<compressor::bitset, uint8_t> bit_dict_;
    
    Data data_;
    EncodedData encoded_data_;
};
    
}

#endif /* Engine_h */
