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

int pingCount, pangCount;

void pang(Fiber<int> &fiber) {
    int value;
    while ((value = *fiber.current()) < TIMES) {
        ++pangCount;
        fiber.yield(value + 1);
    }
}

NOINLINE
void ping() {
    Fiber<int> fiber(pang);

    ++pingCount;
    fiber.next(0);

    while (fiber.next(*fiber.current() + 1)) {
        ++pingCount;
    }
}

int main() {
    auto start = std::chrono::high_resolution_clock::now();
    ping();
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    std::printf("%lldms: pingCount %d, pangCount %d\n", duration, pingCount, pangCount);
}
