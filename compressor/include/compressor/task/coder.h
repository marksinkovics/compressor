#ifndef CoderTask_h
#define CoderTask_h

#include <string>
#include <fstream>

#include <compressor/engine/engine.h>

namespace compressor
{
    
class ICoderTask
{
public:
    virtual ~ICoderTask() = default;
    
    // input
    virtual std::string input_file() = 0;
    // output
    virtual std::string output_file() = 0;

    // engine
    virtual IEngine& engine() = 0;
};

} // compressor

#endif /* CoderTask_h */
