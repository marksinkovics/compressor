#ifndef Compressor_h
#define Compressor_h

#include <fstream>
#include <string>

namespace compressor
{

class Compressor
{
public:
    Compressor(const std::string& input_file, const std::string& output_file);
    ~Compressor();
    void encode();
private:
    std::string input_file_;
    std::string output_file_;
};
    
}

#endif /* Compressor_h */
