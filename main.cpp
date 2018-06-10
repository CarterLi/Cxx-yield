#include <functional>
#include <cassert>
#include <iterator>
#include <iostream>
#ifdef _WIN32
#   define WIN32_LEAD_AND_MEAN
#   include <Windows.h>
#   undef WIN32_LEAD_AND_MEAN
#else
#   include <ucontext.h>
#endif

namespace Fiber {
    /** \brief 辅助类
     *
     * 存放各种静态变量
     * 放于Fiber<...>外，确保类型无关
     */
    class FiberHelper {
        template <typename YieldValueType>
        friend class Fiber;
#ifdef _WIN32
        /// \brief 主纤程信息
        static void* pMainFiber;
#else
        /// \brief 主纤程信息
        static ucontext_t ctx_main;
        /// \brief 子纤程信息
        static ucontext_t ctx_fnew;
        static uint8_t fnew_stack[SIGSTKSZ];
#endif
        /// \brief 子纤程是否结束
        static bool flagFinish;
        static std::exception_ptr eptr;
    };
#ifdef _WIN32
    void* FiberHelper::pMainFiber = nullptr;
#else
    ucontext_t FiberHelper::ctx_main, FiberHelper::ctx_fnew;
    uint8_t FiberHelper::fnew_stack[SIGSTKSZ];
#endif
    bool FiberHelper::flagFinish = false;
    std::exception_ptr FiberHelper::eptr;

    /** \brief 主纤程类
     *
     * 只支持创建一个实例
     * 各种不安全&不支持，玩玩足矣
     *
     * \todo 改用单例模式？
     */
    template <typename YieldValueType>
    class Fiber {
        typedef std::function<YieldValueType ()> FuncType;
        static YieldValueType yieldedValue;

    public:
        /** \brief 构造函数
         *
         * 把主线程转化为纤程，并创建一个子纤程
         *
         * \warning 只支持创建一个Fiber实例
         * \param 子纤程入口
         */
        template <typename Fn>
        Fiber(Fn&& f): func(std::forward<Fn>(f)) {
#ifdef _WIN32
            assert(FiberHelper::pMainFiber == nullptr && "Only one fiber is supported");
#   if _WIN32_WINNT >= 0x0400
            FiberHelper::pMainFiber = ::ConvertThreadToFiberEx(nullptr, FIBER_FLAG_FLOAT_SWITCH);
            // default stack size
            pNewFiber = ::CreateFiberEx(0, 0, FIBER_FLAG_FLOAT_SWITCH, &fEntry, &func);
#   else
#       warning See: msdn.microsoft.com/en-us/library/ms682117.aspx
            FiberHelper::pMainFiber = ::ConvertThreadToFiber(nullptr);
            pNewFiber = ::CreateFiber(0, &fEntry, &func);
#   endif
#else
            ucontext_t* const uctx = &FiberHelper::ctx_fnew;
            ::getcontext(&FiberHelper::ctx_fnew);
            uctx->uc_stack.ss_sp = FiberHelper::fnew_stack;
            uctx->uc_stack.ss_size = sizeof(FiberHelper::fnew_stack);
            uctx->uc_link = &FiberHelper::ctx_main;
            ::makecontext(uctx, reinterpret_cast<void (*)()>(&fEntry), 1, &func);
#endif
            FiberHelper::flagFinish = false;
        }
        /** \brief 析构函数
         *
         * 删除子纤程，并将主纤程转回线程
         *
         * \warning 主类析构时子纤程必须已经结束
         */
        ~Fiber() {
            assert(FiberHelper::flagFinish == true);
#ifdef _WIN32
            ::DeleteFiber(pNewFiber);
            ::ConvertFiberToThread();
            FiberHelper::pMainFiber = nullptr;
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
            assert(FiberHelper::flagFinish == false);
#ifdef _WIN32
            assert(pNewFiber != nullptr);
            ::SwitchToFiber(pNewFiber);
#else
            ::swapcontext(&FiberHelper::ctx_main, &FiberHelper::ctx_fnew);
#endif
            if (!(FiberHelper::eptr == std::exception_ptr()))
                std::rethrow_exception(FiberHelper::eptr);
            return std::move(yieldedValue);
        }

        /** \brief 判断子纤程是否结束
         *
         * \return 子纤程已经结束(return)返回true，否则false
         */
        bool isFinished() { return FiberHelper::flagFinish; }

        /** \brief 转回主纤程并输出值
         *
         * \warning 必须由子纤程调用
         *          参数类型必须与子纤程返回值相同，无类型安全
         * \param 输出到主纤程的值
         */
        static void yield(YieldValueType value) {
            yieldedValue = std::move(value);
#ifdef _WIN32
            assert(FiberHelper::pMainFiber != nullptr);
            assert(GetCurrentFiber() != FiberHelper::pMainFiber);
            ::SwitchToFiber(FiberHelper::pMainFiber);
#else
            ::swapcontext(&FiberHelper::ctx_fnew, &FiberHelper::ctx_main);
#endif
        }

    private:
        /** \brief 子纤程入口的warpper
         *
         * 不足为外人道也
         */
        static void
#ifdef _WIN32
        WINAPI
#endif
        fEntry(void* param) {
            assert(FiberHelper::flagFinish == false);
            assert(param != nullptr);
            FiberHelper::flagFinish = false;
            FuncType& f = *reinterpret_cast<FuncType *>(param);
            try {
                yieldedValue = f();
            } catch (...) { // 没finally淡腾
                FiberHelper::eptr = std::current_exception();
                FiberHelper::flagFinish = true;
#ifdef _WIN32
                ::SwitchToFiber(FiberHelper::pMainFiber);
#else
                return;
#endif
            }
            FiberHelper::flagFinish = true;
#ifdef _WIN32
            ::SwitchToFiber(FiberHelper::pMainFiber);
#endif
        }

#ifdef _WIN32
        /// \brief 子纤程信息
        void* pNewFiber;
#endif
        /// \brief 真子纤程入口
        FuncType func;
    };
    template <typename YieldValueType>
    YieldValueType Fiber<YieldValueType>::yieldedValue;

    /** \brief 纤程迭代器类
     *
     * 它通过使用 yield 函数对数组或集合类执行自定义迭代。
     * 用于 C++11 for (...:...)
     *
     * \bug C++中，iterator的有效范围为[&front, &back+1)
     *      而纤程需要java/.NET的(&front-1, back]
     *      暂时无解，保留待改
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
         *
         * \warning 最好别干别的 ;P
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
    /// 用于 C++11 for (...:...)
    template <typename YieldValueType>
    FiberIterator<YieldValueType> begin(Fiber<YieldValueType>& fiber) {
        return FiberIterator<YieldValueType>(fiber);
    }

    /// \brief 返回迭代器尾
    /// 用于 C++11 for (...:...)
    template <typename YieldValueType>
    FiberIterator<YieldValueType> end(Fiber<YieldValueType>&) {
        return FiberIterator<YieldValueType>();
    }
}

using namespace std;
int Test(int& i) {
    cout << "func, i = " << i << endl;
    // 保留栈，转回主纤程，并输出值
    Fiber::Fiber<int>::yield(++i);
    cout << "func, i = " << i << endl;
    // 终止迭代，返回主纤程，并输出值
    return ++i;
}

int Test2(int beg, int end) {
    while (beg !=end)
        Fiber::Fiber<int>::yield(beg++);
    return beg;
}

int TestException() {
    throw exception();
}

int main() {
    {
        int i = 0, t;
        cout << "main, i = " << i << endl;
        // 把主线程转化为纤程，并创建一个子纤程。参数为子纤程入口
        Fiber::Fiber<int> fiber(std::bind(Test, std::ref(i)));
        // 流程转入子线程
        t = fiber.call();
        cout << "main, Test yield: " << t << endl;
        t = fiber.call();
        cout << "main, Test return: " << t << endl;
        cout << "main, i = " << i << endl;
        // 确保fiber正常析构
    }
    {
        // VC10: ╮(╯▽╰)╭
        for (int i : Fiber::Fiber<int>(std::bind(Test2, 1, 10)))
            cout << i << '\t';
        cout << endl;
    }
    {
        try {
            Fiber::Fiber<int> fiber(TestException);
            fiber.call();
        } catch (exception&) {
            cout << "Exception catched!" << endl;
        }
    }
}
/* Expected Output:
main, i = 0
func, i = 0
main, Test yield: 1
func, i = 1
main, Test return: 2
main, i = 2
0       1       2       3       4       5       6       7       8       9
*/
