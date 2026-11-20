#ifndef cli_h
#define cli_h

#include <functional>
#include <iostream>
#include <memory>
#include <span>

#include <compressor/compressor.h>

namespace compressor
{

// Called after parsing with the --threads value; 0 means all cores.
using CompressorFactory = std::function<std::unique_ptr<ICompressor>(unsigned threads)>;

class CLI
{
public:
    explicit CLI(CompressorFactory make_compressor, std::ostream& out = std::cout,
                 std::ostream& err = std::cerr);
    int run(std::span<const char* const> args);

private:
    CompressorFactory make_compressor_;
    std::ostream& out_;
    std::ostream& err_;
};

} // namespace compressor

#endif /* cli_h */
