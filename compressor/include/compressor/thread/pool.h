#ifndef ThreadPool_h
#define ThreadPool_h

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace compressor
{

// std::thread::hardware_concurrency(), or 1 when it is unknown.
unsigned default_thread_count();

// Fixed workers taking tasks in submission order. A task receives the index of the worker that
// runs it (0 to size() - 1), so per-worker state such as an engine needs no locking. The
// destructor finishes every submitted task.
class ThreadPool
{
public:
    explicit ThreadPool(unsigned threads);
    ~ThreadPool();
    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;
    ThreadPool(ThreadPool&&) = delete;
    ThreadPool& operator=(ThreadPool&&) = delete;

    [[nodiscard]] std::size_t size() const;

    // An exception thrown by the task is rethrown by the future's get().
    template <typename Task>
    auto submit(Task task) -> std::future<std::invoke_result_t<Task&, std::size_t>>
    {
        using Result = std::invoke_result_t<Task&, std::size_t>;
        // std::function needs a copyable target, and packaged_task isn't one.
        auto packaged = std::make_shared<std::packaged_task<Result(std::size_t)>>(std::move(task));
        auto future = packaged->get_future();
        push([packaged](std::size_t worker) {
            (*packaged)(worker);
        });
        return future;
    }

private:
    void push(std::function<void(std::size_t)> task);
    void run(std::size_t worker);

    std::mutex mutex_;
    std::condition_variable ready_;
    std::deque<std::function<void(std::size_t)>> tasks_;
    bool stopping_ = false;
    std::vector<std::jthread> workers_;
};

} // namespace compressor

#endif /* ThreadPool_h */
