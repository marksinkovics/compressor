#ifndef Compressor_h
#define Compressor_h

#include <fstream>
#include <string>

#include <compressor/task/encoder.h>
#include <compressor/task/decoder.h>

namespace compressor
{

class ICompressor
{
public:
    virtual ~ICompressor() = default;
    virtual void encode(IEncoderTask &task) = 0;
    virtual void decode(IDecoderTask& task) = 0;
};

class Compressor: public ICompressor
{
public:
    virtual ~Compressor() = default;
    virtual void encode(IEncoderTask &task);
    virtual void decode(IDecoderTask& task);
};
    
}

#endif /* Compressor_h */
