#ifndef Compressor_h
#define Compressor_h

#include <fstream>
#include <string>

namespace compressor
{

class ICompressor
{
public:
    virtual ~ICompressor() = default;
    virtual void encode() = 0;
};

class Compressor: public ICompressor
{
public:
    Compressor(const std::string& input_file, const std::string& output_file);
    virtual ~Compressor();
    virtual void encode();
private:
    std::string input_file_;
    std::string output_file_;
};
    
}

#endif /* Compressor_h */
