#include <compressor/compressor.h>

#include <iostream>

#include <compressor/data/data.h>
#include <compressor/engine/engine.h>
#include <compressor/error.h>

#include <instrumentor/instrumentor.h>

namespace compressor
{

// The output is opened only after the input has been read, so encoding a file onto itself works
// and a failed decode leaves an existing output untouched.
bool Compressor::encode(IEncoderTask& task)
{
    PROFILE_SESSION("Compressor::Encode")

    PROFILE_START(read_timer, "Read")
    if (!task.reader().open(task.input_file()))
    {
        std::cerr << "Cannot open input file: " << task.input_file() << '\n';
        return false;
    }
    auto decoded_data = task.reader().read();
    task.reader().close();
    PROFILE_END(read_timer, "Read")
    if (!decoded_data)
    {
        std::cerr << "Cannot read " << task.input_file() << ": " << describe(decoded_data.error())
                  << '\n';
        return false;
    }

    PROFILE_START(encode_timer, "Encode")
    auto encoded_data = task.engine().encode(*decoded_data);
    PROFILE_END(encode_timer, "Encode")

    PROFILE_START(write_timer, "Write")
    if (!task.writer().open(task.output_file()))
    {
        std::cerr << "Cannot open output file: " << task.output_file() << '\n';
        return false;
    }
    task.writer().write(encoded_data);
    task.writer().close();
    PROFILE_END(write_timer, "Write")
    return true;
}

bool Compressor::decode(IDecoderTask& task)
{
    PROFILE_SESSION("Compressor::Decode")

    PROFILE_START(read_timer, "Read")
    if (!task.reader().open(task.input_file()))
    {
        std::cerr << "Cannot open input file: " << task.input_file() << '\n';
        return false;
    }
    auto encoded_data = task.reader().read();
    task.reader().close();
    PROFILE_END(read_timer, "Read")
    if (!encoded_data)
    {
        std::cerr << "Cannot read " << task.input_file() << ": " << describe(encoded_data.error())
                  << '\n';
        return false;
    }

    PROFILE_START(decode_timer, "Decode")
    auto decoded_data = task.engine().decode(*encoded_data);
    PROFILE_END(decode_timer, "Decode")
    if (!decoded_data)
    {
        std::cerr << "Cannot decode " << task.input_file() << ": " << describe(decoded_data.error())
                  << '\n';
        return false;
    }

    PROFILE_START(write_timer, "Write")
    if (!task.writer().open(task.output_file()))
    {
        std::cerr << "Cannot open output file: " << task.output_file() << '\n';
        return false;
    }
    task.writer().write(*decoded_data);
    task.writer().close();
    PROFILE_END(write_timer, "Write")
    return true;
}

} // namespace compressor
