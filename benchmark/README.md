# Benchmarks

Running on `MacBook Pro (Retina, 15-inch, Mid 2015)` and mobile phone `Xiaomi Mix 2`

* macOS 10.13.5 ( native )
* Windows 10.1803 ( Bootcamp, native )
* Ubuntu 18.04 ( Virtual machine )
* Android 8.0 ( Termux, native )

Compile on different platforms requires different command arguments. I have modified `Makefile` therefor.

---

| test       | compiler    | platform | boost::fcontext | makecontext | Windows fibers | setjmp / longjmp |
|------------|-------------|----------|-----------------|-------------|----------------|------------------|
| pingpang   | clang/Apple | macOS    | 8ms             | 2353ms      |                |                  |
| createalot | clang/Apple | macOS    | 843ms           | 9930ms      |                |                  |
| pingpang   | g++-8       | macOS    | 4ms             | 2337ms      |                |                  |
| createalot | g++-8       | macOS    | 791ms           | 9637ms      |                |                  |
| pingpang   | g++-7       | Linux    | 4ms             | 317ms       |                |                  |
| createalot | g++-7       | Linux    | 55ms            | 1054ms      |                |                  |
| pingpang   | clang       | Linux    | 8ms             | 293ms       |                |                  |
| createalot | clang       | Linux    | 63ms            | 1060ms      |                |                  |
| pingpang   | clang       | Android  | 40ms            |             |                | crash            |
| createalot | clang       | Android  | 3274ms          |             |                | crash            |
