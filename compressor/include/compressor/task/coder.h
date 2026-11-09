#ifndef CoderTask_h
#define CoderTask_h

#include <fstream>
#include <string>

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

} // namespace compressor

#endif /* CoderTask_h */
