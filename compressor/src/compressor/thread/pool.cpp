#include <compressor/thread/pool.h>

#include <algorithm>
#include <cstddef>
#include <functional>
#include <mutex>
#include <thread>
#include <utility>

namespace compressor
{

unsigned default_thread_count()
{
    return std::max(1U, std::thread::hardware_concurrency());
}

ThreadPool::ThreadPool(unsigned threads)
{
    const unsigned count = std::max(1U, threads);
    workers_.reserve(count);
    for (std::size_t worker = 0; worker < count; ++worker)
    {
        workers_.emplace_back([this, worker] {
            run(worker);
        });
    }
}

ThreadPool::~ThreadPool()
{
    {
        const std::scoped_lock lock(mutex_);
        stopping_ = true;
    }
    ready_.notify_all();
    workers_.clear();
}

std::size_t ThreadPool::size() const
{
    return workers_.size();
}

void ThreadPool::push(std::function<void(std::size_t)> task)
{
    {
        const std::scoped_lock lock(mutex_);
        tasks_.push_back(std::move(task));
    }
    ready_.notify_one();
}

void ThreadPool::run(std::size_t worker)
{
    while (true)
    {
        std::function<void(std::size_t)> task;
        {
            std::unique_lock lock(mutex_);
            ready_.wait(lock, [this] {
                return stopping_ || !tasks_.empty();
            });
            if (tasks_.empty())
            {
                return;
            }
            task = std::move(tasks_.front());
            tasks_.pop_front();
        }
        task(worker);
    }
}

} // namespace compressor
