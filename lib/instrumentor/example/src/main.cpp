#include <cstdlib>
#include <exception>
#include <instrumentor/instrumentor.h>
#include <iostream>
#include <thread>
#include <unistd.h>
#include <vector>

void a()
{
    PROFILE_FUNCTION()
    usleep(50000);
}

void b()
{
    PROFILE_FUNCTION()
    usleep(50000);
}

void f1()
{
    PROFILE_FUNCTION()
    usleep(200000);
    a();
}

void f2()
{
    PROFILE_FUNCTION()
    usleep(200000);
    b();
}

uint64_t factorial(uint64_t n)
{
    PROFILE_FUNCTION()
    usleep(200000);
    if (n == 0 || n == 1)
    {
        return 1;
    }
    return n * factorial(n - 1);
}

void f3()
{
    PROFILE_FUNCTION()
    usleep(200000);
    a();
    b();
    // factorial(15);
    // factorial(20);
}

int main()
try
{
    PROFILE_SESSION("test-main")
    std::vector<std::thread> workers;

    workers.emplace_back(f1);
    workers.emplace_back(f2);
    workers.emplace_back(f3);

    for (auto& w : workers)
    {
        w.join();
    }

    return 0;
}
catch (const std::exception& error)
{
    std::cerr << error.what() << '\n';
    return EXIT_FAILURE;
}
