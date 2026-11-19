#ifndef cli_h
#define cli_h

#include <iostream>
#include <memory>
#include <span>

#include <compressor/compressor.h>

namespace compressor
{

class CLI
{
public:
    explicit CLI(std::unique_ptr<ICompressor> compressor, std::ostream& out = std::cout,
                 std::ostream& err = std::cerr);
    int run(std::span<const char* const> args);

private:
    std::unique_ptr<ICompressor> compressor_;
    std::ostream& out_;
    std::ostream& err_;
};

} // namespace compressor

#endif /* cli_h */
