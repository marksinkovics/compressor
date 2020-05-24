#include <iostream>
#include <instrumentor/instrumentor.h>
#include <unistd.h>
#include <vector>
#include <thread>

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

void f1() {
    PROFILE_FUNCTION()
    usleep(200000);
    a();
}

void f2() {
    PROFILE_FUNCTION()
    usleep(200000);
    b();
}

uint64_t factorial(uint64_t n) {
    PROFILE_FUNCTION()
    usleep(200000);
    if (n == 0 || n == 1) {
        return 1;
    }
    return n * factorial(n-1);
}

void f3() {
    PROFILE_FUNCTION()
    usleep(200000);
    a();
    b();
    // factorial(15);
    // factorial(20);
}

int main()
{
    PROFILE_SESSION("test-main")
    std::vector<std::thread> workers;

    workers.push_back(std::thread(f1));
    workers.push_back(std::thread(f2));
    workers.push_back(std::thread(f3));

    for (auto& w : workers)
    {
        w.join();
    }

    return 0;
}
