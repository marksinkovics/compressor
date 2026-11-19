#include <memory>
#include <span>

#include <compressor/cli.h>

int main(int argc, char* argv[])
{
    compressor::CLI cli(std::make_unique<compressor::Compressor>());
    return cli.run(std::span<const char* const>(argv, static_cast<size_t>(argc)));
}
