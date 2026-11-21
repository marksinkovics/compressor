#include <compressor/gpu/gpu.h>

#import <Foundation/Foundation.h>
#import <Metal/Metal.h>

#include <cstdlib>
#include <map>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <string_view>

// The metallib built from kernels.metal, embedded by CMake (gpu/metallib.cpp).
extern "C" const unsigned char compressor_metallib[];
extern "C" const unsigned long compressor_metallib_size;

namespace compressor::gpu
{

namespace
{

class MetalBuffer : public Buffer
{
public:
    explicit MetalBuffer(id<MTLBuffer> buffer)
        : buffer_(buffer)
    {
    }

    std::span<uint8_t> bytes() override
    {
        return {static_cast<uint8_t*>([buffer_ contents]), [buffer_ length]};
    }

    id<MTLBuffer> handle() const
    {
        return buffer_;
    }

private:
    id<MTLBuffer> buffer_;
};

class MetalDevice : public Device
{
public:
    MetalDevice(id<MTLDevice> device, id<MTLLibrary> library)
        : device_(device)
        , library_(library)
        , queue_([device newCommandQueue])
    {
        const char* name = [[device name] UTF8String];
        name_ = name != nullptr ? name : "GPU";
    }

    std::string_view name() const override
    {
        return name_;
    }

    std::unique_ptr<Buffer> buffer(std::size_t size) override
    {
        // Metal refuses empty buffers; one byte stands for none.
        id<MTLBuffer> buffer = [device_ newBufferWithLength:std::max<std::size_t>(size, 1)
                                                    options:MTLResourceStorageModeShared];
        return buffer == nil ? nullptr : std::make_unique<MetalBuffer>(buffer);
    }

    bool run(std::string_view kernel, std::size_t threads,
             std::initializer_list<Argument> arguments) override
    {
        id<MTLComputePipelineState> pipeline = pipeline_for(kernel);
        if (pipeline == nil)
        {
            return false;
        }
        @autoreleasepool
        {
            id<MTLCommandBuffer> commands = [queue_ commandBuffer];
            id<MTLComputeCommandEncoder> encoder = [commands computeCommandEncoder];
            [encoder setComputePipelineState:pipeline];
            NSUInteger index = 0;
            for (const Argument& argument : arguments)
            {
                if (argument.buffer != nullptr)
                {
                    [encoder setBuffer:static_cast<MetalBuffer*>(argument.buffer)->handle()
                                offset:0
                               atIndex:index];
                }
                else
                {
                    [encoder setBytes:argument.value.data()
                               length:argument.value.size()
                              atIndex:index];
                }
                ++index;
            }
            const NSUInteger group =
                std::min<NSUInteger>([pipeline maxTotalThreadsPerThreadgroup], 256);
            [encoder dispatchThreads:MTLSizeMake(std::max<std::size_t>(threads, 1), 1, 1)
                threadsPerThreadgroup:MTLSizeMake(group, 1, 1)];
            [encoder endEncoding];
            [commands commit];
            [commands waitUntilCompleted];
            return [commands error] == nil;
        }
    }

private:
    id<MTLComputePipelineState> pipeline_for(std::string_view kernel)
    {
        const std::scoped_lock lock(mutex_);
        const std::string key(kernel);
        if (const auto found = pipelines_.find(key); found != pipelines_.end())
        {
            return found->second;
        }
        NSString* name = [NSString stringWithUTF8String:key.c_str()];
        if (name == nil)
        {
            return nil;
        }
        id<MTLFunction> function = [library_ newFunctionWithName:name];
        if (function == nil)
        {
            return nil;
        }
        NSError* error = nil;
        id<MTLComputePipelineState> pipeline = [device_ newComputePipelineStateWithFunction:function
                                                                                      error:&error];
        if (pipeline != nil)
        {
            pipelines_[key] = pipeline;
        }
        return pipeline;
    }

    id<MTLDevice> device_;
    id<MTLLibrary> library_;
    id<MTLCommandQueue> queue_;
    std::string name_;
    std::mutex mutex_;
    std::map<std::string, id<MTLComputePipelineState>> pipelines_;
};

std::unique_ptr<Device> create()
{
    if (std::getenv("COMPRESSOR_NO_GPU") != nullptr)
    {
        return nullptr;
    }
    id<MTLDevice> device = MTLCreateSystemDefaultDevice();
    if (device == nil)
    {
        return nullptr;
    }
    dispatch_data_t data = dispatch_data_create(compressor_metallib, compressor_metallib_size,
                                                nullptr, DISPATCH_DATA_DESTRUCTOR_DEFAULT);
    NSError* error = nil;
    id<MTLLibrary> library = [device newLibraryWithData:data error:&error];
    if (library == nil)
    {
        return nullptr;
    }
    return std::make_unique<MetalDevice>(device, library);
}

} // namespace

Device* device()
{
    static const std::unique_ptr<Device> shared = create();
    return shared.get();
}

} // namespace compressor::gpu
