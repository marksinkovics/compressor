#ifndef Decompressor_h
#define Decompressor_h

#include <fstream>
#include <string>

namespace compressor
{
    
class IDecompressor
{
public:
    virtual ~IDecompressor() = default;
    virtual void decode() = 0;
};

class Decompressor: public IDecompressor
{
public:
    Decompressor(const std::string& input_file, const std::string& output_file);
    virtual ~Decompressor();
    virtual void decode();
private:
    std::string input_file_;
    std::string output_file_;
};
    
} // compressor

#endif /* Decompressor_h */
