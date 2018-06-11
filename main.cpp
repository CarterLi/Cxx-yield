#include "yield.hpp"

using namespace std;
using FiberSpace::Fiber;

bool destructedFlag = true;

struct TestDestruct {
    TestDestruct() {
        destructedFlag = false;
    }
    ~TestDestruct() {
        destructedFlag = true;
    }
};

void foo(Fiber<bool>& fiber, int arg) {
    TestDestruct test;
    for (int i = 1; i < 5; i++) {
        printf("goroutine :%d\n", arg + i);
        fiber.yield();
    }
}

void do_permutation(Fiber<array<int, 4>>& fiber, array<int, 4> arr, int length) {
    if (length) {
        for (auto i = 0; i < length; ++i) {
            array<int, 4> newArr(arr);
            std::copy_n(arr.begin(), i, newArr.begin());
            std::copy_n(arr.begin() + i + 1, arr.size() - i - 1, newArr.begin() + i);
            newArr.back() = arr[i];
            fiber.yieldAll(Fiber<array<int, 4>>(do_permutation, newArr, length - 1));
        }
    }
    else {
        fiber.yield(arr);
    }
}

void permutation(Fiber<array<int, 4>>& fiber, array<int, 4> arr) {
    do_permutation(fiber, arr, (int)arr.size());
}

int main() {
    {
        Fiber<bool> arg1Fiber(foo, 0);
    }
    assert(destructedFlag);
    {
        Fiber<bool> arg1Fiber(foo, 0);
        arg1Fiber.next();
        arg1Fiber.next();
        arg1Fiber.next();
        arg1Fiber.next();
        arg1Fiber.next();
    }
    assert(destructedFlag);
    {
        Fiber<bool> arg1Fiber(foo, 0);
        arg1Fiber.next();
        arg1Fiber.next();
        arg1Fiber.next();
    }
    assert(destructedFlag);
    {
        Fiber<bool> arg1Fiber(foo, 0);
        for (auto&& result : arg1Fiber) {}
    }
    assert(destructedFlag);

    for (auto&& result : Fiber<array<int, 4>>(permutation, array<int, 4> { 1, 2, 3, 4 })) {
        copy(result.begin(), result.end(), std::ostream_iterator<int>(cout, ","));
        cout << endl;
    }
}
