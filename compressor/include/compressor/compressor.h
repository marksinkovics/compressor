#ifndef Compressor_h
#define Compressor_h

#include <fstream>
#include <string>

#include <compressor/task/decoder.h>
#include <compressor/task/encoder.h>

namespace compressor
{

class ICompressor
{
public:
    virtual ~ICompressor() = default;
    virtual bool encode(IEncoderTask& task) = 0;
    virtual bool decode(IDecoderTask& task) = 0;
};

class Compressor : public ICompressor
{
public:
    ~Compressor() override = default;
    bool encode(IEncoderTask& task) override;
    bool decode(IDecoderTask& task) override;
};

} // namespace compressor

#endif /* Compressor_h */
