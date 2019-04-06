#include <compressor/compressor.h>

#include <compressor/data/data.h>
#include <compressor/engine/engine.h>

namespace compressor
{

void Compressor::encode(IEncoderTask &task)
{
    task.reader().open(task.input_file());
    auto decoded_data = task.reader().read();
    task.reader().close();
    
    auto encoded_data = task.engine().encode(decoded_data);

    task.writer().open(task.output_file());
    task.writer().write(encoded_data);
    task.writer().close();
}

void Compressor::decode(IDecoderTask& task)
{
    task.reader().open(task.input_file());
    auto encoded_data = task.reader().read();
    task.reader().close();
    
    auto decoded_data = task.engine().decode(encoded_data);
    
    task.writer().open(task.output_file());
    task.writer().write(decoded_data);
    task.writer().close();
}

}
