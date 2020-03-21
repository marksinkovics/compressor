#include <vector>
#include <filesystem>
#include <iostream>

#include <compressor/cli.h>

std::filesystem::path current_path = std::filesystem::current_path();
std::string current_path_str = current_path.c_str();

std::vector<std::string> inputs {
    "samples/raw/lorem_5p.txt",
    "samples/raw/lorem_10p.txt",
    "samples/raw/lorem_20p.txt",
    "samples/raw/lorem_50p.txt",
    "samples/raw/lena.jpg",
    "samples/raw/flower.png",
};

int main([[maybe_unused]] int argc, char** argv) {
    std::string current_path = std::filesystem::path(argv[0]).remove_filename().c_str();

    for (const auto& input : inputs) {
        std::vector<std::string> arguments = {
            "example",
            "encode",
            "--input",
            current_path + input,
            "--output",
            "/dev/null",
        };

        std::vector<char*> command_arguments;
        for (const auto& arg : arguments)
            command_arguments.push_back((char*)arg.data());
        command_arguments.push_back(nullptr);
        int command_arguments_count = command_arguments.size() - 1;

        compressor::CLI cmd(std::make_unique<argparser::Argparser>(command_arguments_count, command_arguments.data()),
                            std::make_unique<compressor::Compressor>());
        cmd.run();
    }

    return 0;
}
