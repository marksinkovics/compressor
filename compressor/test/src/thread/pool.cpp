#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <future>
#include <mutex>
#include <stdexcept>
#include <vector>

#include <gtest/gtest.h>

#include <compressor/thread/pool.h>

TEST(ThreadPoolTests, Runs_every_task_and_returns_its_result)
{
    compressor::ThreadPool pool(4);
    std::vector<std::future<int>> results;
    results.reserve(100);
    for (int i = 0; i < 100; ++i)
    {
        results.push_back(pool.submit([i](std::size_t /*worker*/) {
            return i * i;
        }));
    }
    for (int i = 0; i < 100; ++i)
    {
        EXPECT_EQ(results[static_cast<std::size_t>(i)].get(), i * i);
    }
}

TEST(ThreadPoolTests, Zero_threads_means_one)
{
    compressor::ThreadPool pool(0);
    EXPECT_EQ(pool.size(), 1U);
    EXPECT_EQ(pool.submit([](std::size_t worker) {
        return worker;
    }).get(),
              0U);
}

TEST(ThreadPoolTests, Worker_indices_are_below_the_size)
{
    compressor::ThreadPool pool(3);
    std::vector<std::future<std::size_t>> workers;
    workers.reserve(50);
    for (int i = 0; i < 50; ++i)
    {
        workers.push_back(pool.submit([](std::size_t worker) {
            return worker;
        }));
    }
    for (auto& worker : workers)
    {
        EXPECT_LT(worker.get(), pool.size());
    }
}

TEST(ThreadPoolTests, Tasks_run_at_the_same_time)
{
    compressor::ThreadPool pool(2);
    std::mutex mutex;
    std::condition_variable arrived;
    int count = 0;
    // Each task waits for the other one, which only works with two running at once.
    const auto meet = [&](std::size_t /*worker*/) {
        std::unique_lock lock(mutex);
        ++count;
        arrived.notify_all();
        return arrived.wait_for(lock, std::chrono::seconds(10), [&] {
            return count == 2;
        });
    };
    auto first = pool.submit(meet);
    auto second = pool.submit(meet);
    EXPECT_TRUE(first.get());
    EXPECT_TRUE(second.get());
}

TEST(ThreadPoolTests, An_exception_reaches_the_future)
{
    compressor::ThreadPool pool(1);
    auto failed = pool.submit([](std::size_t /*worker*/) -> int {
        throw std::runtime_error("task failed");
    });
    EXPECT_THROW((void)failed.get(), std::runtime_error);
    EXPECT_EQ(pool.submit([](std::size_t /*worker*/) {
        return 7;
    }).get(),
              7);
}

TEST(ThreadPoolTests, The_destructor_finishes_queued_tasks)
{
    std::atomic<int> done = 0;
    {
        compressor::ThreadPool pool(2);
        for (int i = 0; i < 20; ++i)
        {
            (void)pool.submit([&done](std::size_t /*worker*/) {
                ++done;
            });
        }
    }
    EXPECT_EQ(done, 20);
}
