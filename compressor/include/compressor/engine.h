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
    
class IEngine
{
public:
    virtual ~IEngine() = default;

    virtual void build_dictionary() noexcept = 0;
    virtual void build_tree() noexcept = 0;
    virtual void create_hash_table() noexcept = 0;
    
    virtual std::shared_ptr<compressor::bitset> find_path(std::shared_ptr<SymbolNode>& node) noexcept = 0;
    virtual void print_graph() const noexcept = 0;
    virtual void print_dict() const noexcept = 0;
    
    virtual EncodedData encode() = 0;
    virtual Data decode() = 0;
};
    
class Engine: public IEngine
{
public:
    Engine(const std::string& input);
    Engine(const Data& data);
    Engine(const EncodedData& data);

    virtual ~Engine();
    
    virtual void build_dictionary() noexcept;
    virtual void build_tree() noexcept;
    virtual void create_hash_table() noexcept;
    
    virtual std::shared_ptr<compressor::bitset> find_path(std::shared_ptr<SymbolNode>& node) noexcept;
    virtual void print_graph() const noexcept;
    virtual void print_dict() const noexcept;
    
    virtual EncodedData encode();
    virtual Data decode();
    
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
