#ifndef Decompressor_h
#define Decompressor_h

#include <fstream>
#include <string>

namespace compressor
{

class Decompressor
{
public:
    Decompressor(const std::string& input_file, const std::string& output_file);
    ~Decompressor();
    void decode();
private:
    std::string input_file_;
    std::string output_file_;
};
    
} // compressor

#endif /* Decompressor_h */
