#ifndef Engine_h
#define Engine_h

#include <array>
#include <cstdint>

#include <compressor/data/data.h>
#include <compressor/huffman/huffman.h>

namespace compressor
{

class IEngine
{
public:
    virtual ~IEngine() = default;

    virtual void print_graph() const = 0;
    virtual void print_dict() const = 0;

    virtual EncodedData encode(const DecodedData& data) = 0;
    virtual DecodedData decode(const EncodedData& data) = 0;
};

class Engine : public IEngine
{
public:
    // Graphviz dot of the code tree of the last encode or decode, rebuilt from the codes.
    void print_graph() const override;
    void print_dict() const override;

    EncodedData encode(const DecodedData& data) override;
    // Returns empty data when the payload ends before data.size_ symbols or holds no valid code.
    DecodedData decode(const EncodedData& data) override;

private:
    void set_codes(const CodeLengths& lengths);

    Frequencies frequencies_{}; // zero after decode: the format stores no frequencies
    Codes codes_{};
};

} // namespace compressor

#endif /* Engine_h */
