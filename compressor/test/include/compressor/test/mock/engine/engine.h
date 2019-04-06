#ifndef MockIEngine_h
#define MockIEngine_h

#include <gmock/gmock.h>

#include <compressor/compressor.h>
#include <compressor/engine/engine.h>

#include <compressor/bitset/bitset.h>
#include <compressor/tree/symbolnode.h>

class MockIEngine: public compressor::IEngine {
public:
    MOCK_METHOD0(build_dictionary, void());
    MOCK_METHOD0(build_tree, void());
    MOCK_METHOD0(create_hash_table, void());
    MOCK_METHOD1(find_path, std::shared_ptr<compressor::bitset>(std::shared_ptr<compressor::SymbolNode>& node));
    MOCK_CONST_METHOD0(print_graph, void());
    MOCK_CONST_METHOD0(print_dict, void());
    MOCK_METHOD1(encode, compressor::EncodedData(const compressor::DecodedData& data));
    MOCK_METHOD1(decode, compressor::DecodedData(const compressor::EncodedData& data));
};

#endif /* MockIEngine_h */
