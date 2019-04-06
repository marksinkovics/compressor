#include <compressor/cli.h>

int main(int argc, char *argv[]) {
    
    compressor::CLI cmd(std::make_unique<argparser::Argparser>(argc, argv),
                        std::make_unique<compressor::Compressor>());
    cmd.run();

    return 0;
}
