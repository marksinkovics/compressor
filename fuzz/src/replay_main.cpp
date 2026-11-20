#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <span>
#include <string>
#include <vector>

// NOLINTNEXTLINE(readability-identifier-naming): libFuzzer's entry point
extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, std::size_t size);

namespace
{

namespace fs = std::filesystem;

bool replay(const fs::path& path)
{
    std::ifstream file(path, std::ios::in | std::ios::binary);
    if (!file)
    {
        std::cerr << "Cannot read " << path.string() << '\n';
        return false;
    }
    const std::vector<char> bytes{std::istreambuf_iterator<char>(file),
                                  std::istreambuf_iterator<char>()};
    LLVMFuzzerTestOneInput(reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size());
    return true;
}

} // namespace

// Without libFuzzer: runs every file given, or every file in the directories given, once.
int main(int argc, char* argv[])
try
{
    std::size_t count = 0;
    for (const char* arg : std::span(argv, static_cast<std::size_t>(argc)).subspan(1))
    {
        const fs::path path(arg);
        std::vector<fs::path> files;
        if (fs::is_directory(path))
        {
            for (const auto& entry : fs::directory_iterator(path))
            {
                files.push_back(entry.path());
            }
        }
        else
        {
            files.push_back(path);
        }
        for (const fs::path& file : files)
        {
            if (!replay(file))
            {
                return 1;
            }
            ++count;
        }
    }
    std::cout << "Replayed " << count << " inputs\n";
    return count > 0 ? 0 : 1;
}
catch (const std::exception& e)
{
    std::cerr << e.what() << '\n';
    return 1;
}
