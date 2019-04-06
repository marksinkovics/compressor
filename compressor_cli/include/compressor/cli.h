#ifndef cli_h
#define cli_h

#include <memory>

#include <argparser/argparser.h>
#include <compressor/compressor.h>

namespace compressor
{

class CLI
{
public:
    CLI(std::unique_ptr<argparser::IArgparser>&& parser,
        std::unique_ptr<compressor::ICompressor>&& compressor);
    void run();
private:
    std::unique_ptr<argparser::IArgparser> parser_;
    std::unique_ptr<compressor::ICompressor> compressor_;
    argparser::IArgparser::container_type options_;
};
    
} // compressor

#endif /* cli_h */
