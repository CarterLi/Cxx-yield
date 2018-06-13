# Benchmarks

Running on `MacBook Pro (Retina, 15-inch, Mid 2015)` and mobile phone `Xiaomi Mix 2`

* macOS 10.13.5 ( native )
* Ubuntu 18.04 ( Virtual machine )
* Windows 10.1803 ( Bootcamp, native )
* Android 8.0 ( Termux, native )

Compiling on different platforms requires different command arguments. I have modified `Makefile` therefor.

For clang, `-std=libc++ -lc++` is used. Note Don't use `-lc++abi` on Linux as `exception_ptr` is not yet implemented on Linux.

## Result

| test       | compiler         | platform | boost::fcontext | makecontext | Windows fibers | setjmp / longjmp |
|------------|------------------|----------|-----------------|-------------|----------------|------------------|
| pingpang   | AppleClang-9.1.0 | macOS    | 9ms             | 2324ms      |                | CRASH            |
| createalot | AppleClang-9.1.0 | macOS    | 830ms           | 9651ms      |                | CRASH            |
| pingpang   | clang-6.0.0      | macOS    | 8ms             | 2350ms      |                | CRASH            |
| createalot | clang-6.0.0      | macOS    | 866ms           | 9953ms      |                | CRASH            |
| pingpang   | g++-8.1.0        | macOS    | 4ms             | 2337ms      |                | CRASH            |
| createalot | g++-8.1.0        | macOS    | 791ms           | 9637ms      |                | CRASH            |
| pingpang   | g++-7.3.0        | Linux    | 3ms             | 299ms       |                | CRASH            |
| createalot | g++-7.3.0        | Linux    | 53ms            | 1052ms      |                | CRASH            |
| pingpang   | clang-6.0.0      | Linux    | 7ms             | 281ms       |                | 19ms !           |
| createalot | clang-6.0.0      | Linux    | 52ms            | 984ms       |                | 2980ms !         |
| pingpang   | msvc-15.7.3      | Windows  | 21ms            |             | 37ms           |                  |
| createalot | msvc-15.7.3      | Windows  | 265ms           |             | 12997ms        |                  |
| pingpang   | g++-8            | Windows  | 11ms            |             | 36ms           |                  |
| createalot | g++-8            | Windows  | 204ms           |             | 16135ms        |                  |
| pingpang   | clang-6.0.0      | Android  | 36ms            |             |                | 78ms             |
| createalot | clang-6.0.0      | Android  | 3392ms          |             |                | 3994ms           |

## Conclusion

* Performance on differenct platforms varies a lot, but different compilers on the same platform performs similarly.
* `boost::fcontext` has the best performance, without question.
* `makecontext` on macOS performs poorly, don't use it.
* `Windows fibers` have good context switching performance, but creating new fibers is VERY expensive. Use it with care.
* `setjmp / longjmp` still has bugs...
