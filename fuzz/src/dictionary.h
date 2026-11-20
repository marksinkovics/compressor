#ifndef FuzzDictionary_h
#define FuzzDictionary_h

#include <memory>
#include <string_view>
#include <vector>

#include <compressor/dictionary/dictionary.h>

namespace fuzz
{

// The dictionary fuzz_container decodes with and the coder 9 to 12 seeds are made with.
inline std::shared_ptr<const compressor::Dictionary> dictionary()
{
    static constexpr std::string_view text =
        "index block archive\nmatch huffman seed, index block archive\nmatch huffman seed, "
        "{\"name\": \"seed\", \"version\": \"1.0\", \"dependencies\": [\"index\", \"block\"]}\n";
    // Structured, with codes trained on the text itself, so coder 12 has seeds too.
    static const auto shared = [] {
        const std::vector<uint8_t> content(text.begin(), text.end());
        const std::vector<std::vector<uint8_t>> samples{content};
        return std::make_shared<const compressor::Dictionary>(*compressor::parse_dictionary(
            compressor::write_dictionary_file(content, compressor::train_codes(samples, content))));
    }();
    return shared;
}

} // namespace fuzz

#endif /* FuzzDictionary_h */
