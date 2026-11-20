#ifndef FuzzDictionary_h
#define FuzzDictionary_h

#include <memory>
#include <string_view>
#include <vector>

#include <compressor/dictionary/dictionary.h>

namespace fuzz
{

// The dictionary fuzz_container decodes with and the coder 9 and 10 seeds are made with.
inline std::shared_ptr<const compressor::Dictionary> dictionary()
{
    static constexpr std::string_view text =
        "index block archive\nmatch huffman seed, index block archive\nmatch huffman seed, "
        "{\"name\": \"seed\", \"version\": \"1.0\", \"dependencies\": [\"index\", \"block\"]}\n";
    static const auto shared = std::make_shared<const compressor::Dictionary>(
        compressor::make_dictionary(std::vector<uint8_t>(text.begin(), text.end())));
    return shared;
}

} // namespace fuzz

#endif /* FuzzDictionary_h */
