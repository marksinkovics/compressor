#include <memory>
#include <span>

#ifdef _WIN32
#include <cstdio>
#include <fcntl.h>
#include <io.h>
#endif

#include <compressor/cli.h>

int main(int argc, char* argv[])
{
#ifdef _WIN32
    // Text mode would translate line endings in the compressed data.
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    compressor::CLI cli([](unsigned threads) {
        return std::make_unique<compressor::Compressor>(threads);
    });
    return cli.run(std::span<const char* const>(argv, static_cast<size_t>(argc)));
}
