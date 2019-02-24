#ifndef cli_h
#define cli_h

#include <memory>

#include <argparser/argparser.h>

namespace compressor
{
    
class CLI
{
public:
    CLI(int argc, char *argv[]);
    void run();
private:
    std::shared_ptr<argparser::Argparser> parser_;
    argparser::Argparser::container_type options_;
};

} // compressor

#endif /* cli_h */
