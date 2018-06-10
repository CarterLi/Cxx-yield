#include <functional>
#include <cassert>
#include <iterator>
#include <iostream>
#ifdef _WIN32
#   ifdef WIN32_LEAD_AND_MEAN
#       include <Windows.h>
#   else
#       define WIN32_LEAD_AND_MEAN 1
#       include <Windows.h>
#       undef WIN32_LEAD_AND_MEAN
#   endif
#else
#   include <ucontext.h>
#   include <memory>
#endif

namespace FiberSpace {
    class FiberHelper {
        template <typename YieldValueType>
        friend class Fiber;

#if defined(_WIN32) && (!defined(_WIN32_WINNT) || (_WIN32_WINNT) < 0x600)
        // Vista之前不支持IsThreadAFiber
        /// \brief 表示是否需要线纤互转
        static bool bNeedConvert;
#endif
        /// \brief 存放当前纤程的param指针
        static void* paraThis;
    };
#if defined(_WIN32) && (!defined(_WIN32_WINNT) || (_WIN32_WINNT) < 0x600)
    bool FiberHelper::bNeedConvert = true;
#endif
    void* FiberHelper::paraThis = nullptr;

    /** \brief 主纤程类
     *
     * 目前可以嵌套开纤程，但最好只在每个纤程中新开一个纤程
     *
     * \warning 无线程安全
     * \param 子纤程返回类型
     */
    template <typename YieldValueType>
    class Fiber {
        static_assert(!std::is_same<YieldValueType, void>::value, "A fiber which return void is unsupported");
        typedef std::function<YieldValueType ()> FuncType;

        /** \brief 纤程参数结构体
         *
         * 通过子纤程入口函数参数传入子纤程
         */
        struct Param {
            template <typename Fn>
            Param(Fn&& f)
                : func(std::forward<Fn>(f))
#ifndef _WIN32
                // 只能放在堆里，否则SIGSRV，原因不知
                , fnew_stack(new uint8_t[SIGSTKSZ])
#endif
                {};
            /// \brief 子纤程返回值
            YieldValueType yieldedValue;
            /// \brief 存储子纤程抛出的异常
            std::exception_ptr eptr;
            /// \brief 子纤程是否结束
            bool flagFinish;
            /// \brief 真子纤程入口
            FuncType func;
            /// \brief 纤程信息
#ifdef _WIN32
            PVOID pMainFiber, pNewFiber;
            bool isFormerAThread;
#else
            ucontext_t ctx_main, ctx_fnew;
            const std::unique_ptr<uint8_t[]> fnew_stack;
#endif
        } param;

    public:
        /** \brief 构造函数
         *
         * 把主线程转化为纤程，并创建一个子纤程
         *
         * \param 子纤程入口
         */
        template <typename Fn>
        Fiber(Fn&& f): param(std::forward<Fn>(f)) {
#ifdef _WIN32
#   if defined(_WIN32_WINNT) && (_WIN32_WINNT) > 0x600
            // 貌似MinGW杯具？
            param.isFormerAThread = IsThreadAFiber() == FALSE;
#   else
            param.isFormerAThread = FiberHelper::bNeedConvert;
#   endif
            if (param.isFormerAThread) {
#   if defined(_WIN32_WINNT) && (_WIN32_WINNT) > 0x502
                param.pMainFiber = ::ConvertThreadToFiberEx(nullptr, FIBER_FLAG_FLOAT_SWITCH);
#   else
#       warning See: msdn.microsoft.com/en-us/library/ms682117
                param.pMainFiber = ::ConvertThreadToFiber(nullptr);
#   endif
#   if !(defined(_WIN32_WINNT) && (_WIN32_WINNT) > 0x600)
                FiberHelper::bNeedConvert = false;
#   endif
            } else {
                param.pMainFiber = ::GetCurrentFiber();
            }
            // default stack size
#   if defined(_WIN32_WINNT) && (_WIN32_WINNT) > 0x502
            param.pNewFiber = ::CreateFiberEx(0, 0, FIBER_FLAG_FLOAT_SWITCH, &fEntry, &param);
#   else
#       warning See: msdn.microsoft.com/en-us/library/ms682406
            param.pNewFiber = ::CreateFiber(0, &fEntry, nullptr);
#   endif
#else
            ::getcontext(&param.ctx_fnew);
            param.ctx_fnew.uc_stack.ss_sp = param.fnew_stack.get();
            param.ctx_fnew.uc_stack.ss_size = SIGSTKSZ;
            param.ctx_fnew.uc_link = &param.ctx_main;
            ::makecontext(&param.ctx_fnew, &fEntry, 0);
#endif
            param.flagFinish = false;
        }
        /** \brief 析构函数
         *
         * 删除子纤程，并将主纤程转回线程
         *
         * \warning 主类析构时子纤程必须已经结束(return)
         */
        ~Fiber() {
            if (!isFinished())
                std::terminate();
#ifdef _WIN32
            ::DeleteFiber(param.pNewFiber);
            if (param.isFormerAThread) {
                ::ConvertFiberToThread();
#   if !(defined(_WIN32_WINNT) && (_WIN32_WINNT) > 0x600)
                FiberHelper::bNeedConvert = true;
#   endif
            }
#endif
        }

        /** \brief 调用子纤程
         *
         * 程序流程转入子线程
         *
         * \warning 子纤程必须尚未结束
         * \return 返回子纤程yield或return的值
         */
        YieldValueType call() {
            assert(isFinished() == false);
            void* oldPara = FiberHelper::paraThis;
            FiberHelper::paraThis = &param;
#ifdef _WIN32
            ::SwitchToFiber(param.pNewFiber);
#else
            ::swapcontext(&param.ctx_main, &param.ctx_fnew);
#endif
            FiberHelper::paraThis = oldPara;
            if (!(param.eptr == std::exception_ptr()))
                std::rethrow_exception(param.eptr);
            return std::move(param.yieldedValue);
        }

        /** \brief 判断子纤程是否结束
         * \return 子纤程已经结束(return)返回true，否则false
         */
        bool isFinished() { return param.flagFinish; }

        /** \brief 转回主纤程并输出值
         *
         * \warning 必须由子纤程调用
         *          参数类型必须与子纤程返回值相同，无类型安全
         * \param 输出到主纤程的值
         */
        static void yield(YieldValueType value) {
            assert(FiberHelper::paraThis != nullptr && "Fiber::yield() called with no active fiber");
            Param& param = *reinterpret_cast<Param *>(FiberHelper::paraThis);
            param.yieldedValue = std::move(value);
#ifdef _WIN32
            ::SwitchToFiber(param.pMainFiber);
#else
            ::swapcontext(&param.ctx_fnew, &param.ctx_main);
#endif
        }

    private:
        /// \brief 子纤程入口的warpper

#ifdef _WIN32
        static void WINAPI fEntry(void *) {
#else
        static void fEntry() {
#endif
            assert(FiberHelper::paraThis != nullptr);
            Param& param = *reinterpret_cast<Param *>(FiberHelper::paraThis);
            param.flagFinish = false;
            try {
                param.yieldedValue = param.func();
            } catch (...) {
                param.eptr = std::current_exception();
            }
            param.flagFinish = true;
#ifdef _WIN32
            ::SwitchToFiber(param.pMainFiber);
#endif
        }
    };

    /** \brief 纤程迭代器类
     *
     * 它通过使用 yield 函数对数组或集合类执行自定义迭代。
     * 用于 C++11 for (...:...)
     */
    template <typename YieldValueType>
    struct FiberIterator: std::iterator<std::input_iterator_tag, YieldValueType> {
        /// \brief 迭代器尾
        FiberIterator(): fiber(nullptr), value() {}
        /** \brief 迭代器首
         * \param 主线程类的引用
         */
        FiberIterator(Fiber<YieldValueType>& _f): fiber(&_f), value(_f.call()) {}

        /// \brief 转入子纤程
        FiberIterator& operator ++() {
            assert(fiber != nullptr);
            if (!fiber->isFinished())
                value = fiber->call();
            else
                fiber = nullptr;
            return *this;
        }
        // 返回临时对象没问题吧-_-!!
        FiberIterator operator ++(int) {
            FiberIterator tmp(*this);
            ++*this;
            return tmp;
        }

        /// \brief 取得返回值
        YieldValueType& operator *() {
            assert(fiber != nullptr);
            return value;
        }

        /** \brief 比较迭代器相等
         *
         * 通常用于判断迭代是否结束
         * 最好别干别的 ;P
         */
        bool operator ==(const FiberIterator& rhs) {
            return fiber == rhs.fiber;
        }
        bool operator !=(const FiberIterator& rhs) {
            return !(*this == rhs);
        }

    private:
        Fiber<YieldValueType>* fiber;
        YieldValueType value;
    };

    /// \brief 返回迭代器首
    template <typename YieldValueType>
    FiberIterator<YieldValueType> begin(Fiber<YieldValueType>& fiber) {
        return FiberIterator<YieldValueType>(fiber);
    }

    /// \brief 返回迭代器尾
    template <typename YieldValueType>
    FiberIterator<YieldValueType> end(Fiber<YieldValueType>&) {
        return FiberIterator<YieldValueType>();
    }
}

using namespace std;
using FiberSpace::Fiber;

int Test(int& i) {
    cout << "func, i = " << i << endl;
    // 保留栈，转回主纤程，并输出值
    Fiber<int>::yield(++i);
    cout << "func, i = " << i << endl;
    // 终止迭代，返回主纤程，并输出值
    return ++i;
}

int Test2(int beg, int end) {
    while (beg !=end)
        Fiber<int>::yield(beg++);
    return beg;
}

long Test3() {
    auto testF = [] () -> int {
        Fiber<int>::yield(1);
        Fiber<int>::yield(2);
        return 3;
    };
    for (auto i : Fiber<int>(testF))
        Fiber<long>::yield(i);
    return 4;
}

long TestException() {
    auto testF = [] () -> int {
        Fiber<int> fiber([] () -> int { throw exception(); return 0; });
        return fiber.call();
    };
    try {
        return Fiber<int>(testF).call();
    } catch (...) {
        cout << "Exception catched in TestException()" << endl;
        throw;
    }
}

class DerivedFiber: public Fiber<char> {
public:
    DerivedFiber(): Fiber(std::bind(&DerivedFiber::run, this)) {}

private:
    char run() {
        puts("Derived fiber running.");
        return 0;
    }
};

char fiberFunc() {
    puts("Composed fiber running.");
    Fiber<char>::yield(0);
    puts("Composed fiber running.");
    return 0;
}

int main() {
    {
    // 测试基本流程转换
    int i = 0, t;
    cout << "main, i = " << i << endl;
    // 把主线程转化为纤程，并创建一个子纤程。参数为子纤程入口
    Fiber<int> fiber(std::bind(Test, std::ref(i)));
    // 流程转入子线程
    t = fiber.call();
    cout << "main, Test yield: " << t << endl;
    t = fiber.call();
    cout << "main, Test return: " << t << endl;
    cout << "main, i = " << i << endl;
    // 确保fiber正常析构
    }
    {
    // Test from dlang.org
    // create instances of each type
    unique_ptr<DerivedFiber> derived(new DerivedFiber);
    unique_ptr<Fiber<char>> composed(new Fiber<char>(&fiberFunc));

    // call both fibers once
    derived->call();
    composed->call();
    puts("Execution returned to calling context.");
    composed->call();

    // since each fiber has run to completion, each should have state TERM
    assert( derived->isFinished() );
    assert( composed->isFinished() );
    }
    // 测试循环yield
    for (Fiber<int> fiber(std::bind(Test2, 1, 10)); !fiber.isFinished();)
        cout << fiber.call() << ' ';
    cout << endl;

    // 测试返回非基本类型，foreach
    // VC10: ╮(╯▽╰)╭
    for (const string& s : Fiber<string>([] () -> std::string {
        Fiber<string>::yield("Hello");
        Fiber<string>::yield("World");
        return "!!!!!";
    })) {
        cout << s << endl;
    }

    // 测试深层调用、返回
    for (auto i : Fiber<long>(Test3))
        cout << i << ' ';
    cout << endl;

#if !(defined(__GNUC__) && defined(_WIN32))
    // 测试深层调用及异常安全
    // Test fail with MinGW
    try {
        Fiber<long> fiber(TestException);
        fiber.call();
    } catch (exception&) {
        cout << "Exception catched in main()!" << endl;
    }
#endif
}
/*
Tested with VS2010(icl), MinGW-g++, VMware-archlinux-g++
Expected Output:
main, i = 0
func, i = 0
main, Test yield: 1
func, i = 1
main, Test return: 2
main, i = 2
Derived fiber running.
Composed fiber running.
Execution returned to calling context.
Composed fiber running.
1 2 3 4 5 6 7 8 9 10
Hello
World
!!!!!
1 2 3 4
Exception catched in TestException()
Exception catched in main()!
*/
