#include <chrono>
#include <cstdio>
#include "../yield.hpp"

#ifdef _MSC_VER
#   define NOINLINE __declspec(noinline)
#else
#   define NOINLINE __attribute__((noinline))
#endif

#ifndef TIMES
#   define TIMES 999999
#endif

using namespace FiberSpace;

int enterCount;

void fn(Fiber<int, true> &fiber) noexcept {
    ++enterCount;
}

NOINLINE
void test() noexcept {
    int count = TIMES;
    while (count --> 0) {
        Fiber<int, true> fiber(fn);
        fiber.next();
    }
}

int main() {
    auto start = std::chrono::high_resolution_clock::now();
    test();
    auto end = std::chrono::high_resolution_clock::now();
    long long duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    std::printf("%lldms: enterCount %d\n", duration, enterCount);
}
