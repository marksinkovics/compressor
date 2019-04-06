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

    virtual void build_dictionary() = 0;
    virtual void build_tree() = 0;
    virtual void create_hash_table() = 0;
    
    virtual std::shared_ptr<compressor::bitset> find_path(std::shared_ptr<SymbolNode>& node) = 0;
    virtual void print_graph() const = 0;
    virtual void print_dict() const = 0;
    
    virtual EncodedData encode(const DecodedData& data) = 0;
    virtual DecodedData decode(const EncodedData& data) = 0;
};
    
class Engine: public IEngine
{
public:
    virtual ~Engine();
    
    virtual void build_dictionary();
    virtual void build_tree();
    virtual void create_hash_table();
    
    virtual std::shared_ptr<compressor::bitset> find_path(std::shared_ptr<SymbolNode>& node);
    virtual void print_graph() const;
    virtual void print_dict() const;
    
    virtual EncodedData encode(const DecodedData& data);
    virtual DecodedData decode(const EncodedData& data);
    
private:
    std::shared_ptr<BinaryTree> _tree;
    std::map<uint8_t, std::shared_ptr<SymbolNode>> _symbols_dict;
    
    std::map<uint8_t, compressor::bitset> byte_dict_;
    std::map<compressor::bitset, uint8_t> bit_dict_;
    
    DecodedData data_;
    EncodedData encoded_data_;
};
    
}

#endif /* Engine_h */
