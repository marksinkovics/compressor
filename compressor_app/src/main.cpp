#include "cli.h"

int main(int argc, char *argv[]) {
    
    compressor::CLI cmd(argc, argv);
    cmd.run();

    return 0;
}
