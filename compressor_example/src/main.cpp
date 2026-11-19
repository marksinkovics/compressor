#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include <compressor/cli.h>

int main([[maybe_unused]] int argc, char** argv)
{
    const std::vector<std::string> inputs{
        "samples/raw/lorem_5p.txt",  "samples/raw/lorem_10p.txt", "samples/raw/lorem_20p.txt",
        "samples/raw/lorem_50p.txt", "samples/raw/lena.jpg",      "samples/raw/flower.png",
    };
    const std::string current_path = std::filesystem::path(argv[0]).remove_filename().string();

    for (const auto& input : inputs)
    {
        const std::string input_path = current_path + input;
        const std::vector<const char*> args = {
            "example", "encode", "--input", input_path.c_str(), "--output", "/dev/null",
        };

        compressor::CLI cli(std::make_unique<compressor::Compressor>());
        cli.run(args);
    }

    return 0;
}
