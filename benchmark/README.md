# Benchmarks

Running on `MacBook Pro (Retina, 15-inch, Mid 2015)` and mobile phone `Xiaomi Mix 2`

* macOS 10.13.5 ( native )
* Windows 10 ( Bootcamp, native )
* Ubuntu 18.04 ( Virtual machine )
* Android 8.0 ( Termux, native )

Compile on different platforms requires different command arguments. I have modified `Makefile` therefor.

---

| test       | compiler    | platform | boost::fcontext | makecontext | Windows fibers | setjmp / longjmp |
|------------|-------------|----------|-----------------|-------------|----------------|------------------|
| pingpang   | clang/Apple | macOS    | 28ms            | 2393ms      |                |                  |
| createalot | clang/Apple | macOS    | 5847ms          | 9996ms      |                |                  |
| pingpang   | g++-8       | macOS    | 45ms            | 2378ms      |                |                  |
| createalot | g++-8       | macOS    | 6791ms          | 9920ms      |                |                  |
| pingpang   | g++-7       | Linux    | 63ms            | 358ms       |                |                  |
| createalot | g++-7       | Linux    | 5447ms          | 1319ms      |                |                  |
| pingpang   | clang       | Linux    | 73ms            | 366ms       |                |                  |
| createalot | clang       | Linux    | 5204ms          | 1188ms      |                |                  |
| pingpang   | clang       | Android  | 102ms           |             |                | 134ms            |
| createalot | clang       | Android  | 5275ms          |             |                | 4320ms           |
