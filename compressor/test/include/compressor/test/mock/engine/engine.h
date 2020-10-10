#ifndef MockIEngine_h
#define MockIEngine_h

#include <gmock/gmock.h>

#include <compressor/compressor.h>
#include <compressor/engine/engine.h>

#include <compressor/bitset/bitset.h>
#include <compressor/tree/symbolnode.h>

class MockIEngine: public compressor::IEngine {
public:
    MOCK_METHOD(void, build_dictionary,(), (override));
    MOCK_METHOD(void, build_tree, (), (override));
    MOCK_METHOD(void, create_hash_table, (), (override));
    using SharedPtrBitset = std::shared_ptr<compressor::bitset>;
    using SharedNode = std::shared_ptr<compressor::SymbolNode>;
    MOCK_METHOD(SharedPtrBitset, find_path, (SharedNode&), (override));
    MOCK_METHOD(void, print_graph, (), (override, const));
    MOCK_METHOD(void, print_dict, (), (override, const));
    MOCK_METHOD(compressor::EncodedData, encode, (const compressor::DecodedData&), (override));
    MOCK_METHOD(compressor::DecodedData, decode, (const compressor::EncodedData&), (override));
};

#endif /* MockIEngine_h */
