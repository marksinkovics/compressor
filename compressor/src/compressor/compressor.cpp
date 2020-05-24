#include <compressor/compressor.h>

#include <compressor/data/data.h>
#include <compressor/engine/engine.h>

#include <instrumentor/instrumentor.h>

namespace compressor
{

void Compressor::encode(IEncoderTask &task)
{
    PROFILE_SESSION("Compressor::Encode")

    PROFILE_START(read_timer, "Read")
    task.reader().open(task.input_file());
    auto decoded_data = task.reader().read();
    task.reader().close();
    PROFILE_END(read_timer, "Read")

    PROFILE_START(encode_timer, "Encode")
    auto encoded_data = task.engine().encode(decoded_data);
    PROFILE_END(encode_timer, "Encode")

    PROFILE_START(write_timer, "Write")
    task.writer().open(task.output_file());
    task.writer().write(encoded_data);
    task.writer().close();
    PROFILE_END(write_timer, "Write")
}

void Compressor::decode(IDecoderTask& task)
{
    PROFILE_SESSION("Compressor::Decode")

    PROFILE_START(read_timer, "Read")
    task.reader().open(task.input_file());
    auto encoded_data = task.reader().read();
    task.reader().close();
    PROFILE_END(read_timer, "Read")

    PROFILE_START(decode_timer, "Decode")
    auto decoded_data = task.engine().decode(encoded_data);
    PROFILE_END(decode_timer, "Decode")

    PROFILE_START(write_timer, "Write")
    task.writer().open(task.output_file());
    task.writer().write(decoded_data);
    task.writer().close();
    PROFILE_END(write_timer, "Write")
}

}
