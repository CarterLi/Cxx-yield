# Cxx-Yield

JavaScript Generator like header-only library written in C++.

See `yield.hpp` for detail

## Implementations used

### boost::fcontext ( currently 1.67.0 )

Doc: <https://www.boost.org/doc/libs/1_67_0/libs/context/doc/html/index.html>

Best performance, best portability, but requires the HUGE boost library.

Will use it when boost is detected. Pass `-lboost_context` or `-lboost_context-md` when linking.

### Windows fibers

Doc: <https://msdn.microsoft.com/en-us/library/windows/desktop/ms682661(v=vs.85).aspx>

Good performance, Windows 7 or later only.

Said by offical [boost doc](https://www.boost.org/doc/libs/1_67_0/libs/context/doc/html/context/ff/implementations__fcontext_t__ucontext_t_and_winfiber.html)

> Because the TIB (thread information block on Windows) is not fully described in the MSDN, it might be possible that not all required TIB-parts are swapped. Using WinFiber implementation migh be an alternative.

Note, like what boost do:

> The first call of fiber converts the thread into a Windows fiber by invoking ConvertThreadToFiber(). If desired, ConvertFiberToThread() has to be called by the user explicitly in order to release resources allocated by ConvertThreadToFiber()

### ucontext_t

Doc: <http://man7.org/linux/man-pages/man3/makecontext.3.html>

Acceptable performance, *nix ( known works on Linux & macOS, but unfortunately Android ) support.

Said by [boost](https://www.boost.org/doc/libs/1_67_0/libs/context/doc/html/context/rationale/other_apis_.html#ucontext)

> The arguments in the var-arg list are required to be integers, passing pointers in var-arg list is not guaranteed to work, especially it will fail for architectures where pointers are larger than integers.

but the [doc](http://man7.org/linux/man-pages/man3/makecontext.3.html) said

> Nevertheless, starting with version 2.8, glibc makes some changes to makecontext(), to permit this on some 64-bit architectures (e.g., x86-64).

And said by [boost](https://www.boost.org/doc/libs/1_67_0/libs/context/doc/html/context/rationale/other_apis_.html#ucontext)

> ucontext_t preserves signal mask between context switches which involves system calls consuming a lot of CPU cycles

### setjmp / longjmp

Doc: <http://man7.org/linux/man-pages/man3/setjmp.3.html>

Worst performace, but support Android.

It's highly experimental and unstable ( It compiles on macOS too but crashes at runtime ), just for testing purpose. I wrote the code without knowing how signal works well. Use it as your own risk.

And also said by [boost](https://www.boost.org/doc/libs/1_67_0/libs/context/doc/html/context/rationale/other_apis_.html#context.rationale.other_apis_.setjmp___longjmp__)

> C99 defines setjmp()/longjmp() to provide non-local jumps but it does not require that longjmp() preserves the current stack frame. Therefore, jumping into a function which was exited via a call to longjmp() is undefined

## Tests and examples

See `main.cpp`
