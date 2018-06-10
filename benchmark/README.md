# Benchmarks

Running on `MacBook Pro (Retina, 15-inch, Mid 2015)` and my mobile phone `Xiaomi Mix 2`

* macOS 10.13.5 ( native )
* Windows 10 ( Bootcamp, native )
* Ubuntu 18.04 ( Virtual machine )
* Android 8.0 ( native )

---

| test       | platform | boost::fcontext | makecontext | Windows fibers | setjmp / longjmp |
|------------|----------|-----------------|-------------|----------------|------------------|
| pingpang   | macOS    | 28ms            | 2353ms      |                |                  |
| createalot | macOS    | 6196ms          | 9950ms      |                |                  |
