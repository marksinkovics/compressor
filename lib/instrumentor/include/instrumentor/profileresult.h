#ifndef ProfileResult_h
#define ProfileResult_h

#include <string>

struct ProfileResult {
    const std::string name;
    long long start, end;
    uint64_t threadID;
};

#endif /* ProfileResult_h */
