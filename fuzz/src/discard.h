#ifndef FuzzDiscard_h
#define FuzzDiscard_h

#include <ios>
#include <streambuf>

namespace fuzz
{

class DiscardBuf : public std::streambuf
{
protected:
    std::streamsize xsputn(const char* /*data*/, std::streamsize count) override
    {
        return count;
    }

    int_type overflow(int_type c) override
    {
        return traits_type::not_eof(c);
    }
};

} // namespace fuzz

#endif /* FuzzDiscard_h */
