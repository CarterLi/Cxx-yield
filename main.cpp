#ifdef _WIN32
#   ifdef _WIN32_WINNT
#       if _WIN32_WINNT < 0x0601
#           error 需要 Windows 7 以上系统支持
#       endif
#   else
#       define _WIN32_WINNT 0x0601
#   endif
#   ifdef WIN32_LEAD_AND_MEAN
#       include <Windows.h>
#   else
#       define WIN32_LEAD_AND_MEAN 1
#       include <Windows.h>
#       undef WIN32_LEAD_AND_MEAN
#   endif
#else
#   if defined(__APPLE__)
#       define _XOPEN_SOURCE
#   endif
#   include <ucontext.h>
#endif

#include <functional>
#include <cassert>
#include <iterator>
#include <iostream>
#include <array>
#include <memory>
#include <algorithm>

namespace FiberSpace {
    enum class FiberStatus {
        unstarted = -1,
        running = 1,
        suspended = 2,
        closed = 0,
    };


    /** \brief 主纤程类析构异常类
     *
     * \warning 用户代码吃掉此异常可导致未定义行为。如果捕获到此异常，请转抛出去。
     */
    struct FiberReturn {
        template <typename YieldValueType>
        friend class Fiber;

    private:
        FiberReturn() = default;
    };

    /** \brief 主纤程类
     *
     * \warning 线程安全（？）
     * \param YieldValueType 子纤程返回类型
     */
    template <typename YieldValueType = void>
    class Fiber {
        Fiber(const Fiber &) = delete;
        Fiber& operator =(const Fiber &) = delete;

        typedef std::function<void (Fiber& fiber)> FuncType;

        /// \brief 子纤程返回值
        YieldValueType yieldedValue = YieldValueType();
        /// \brief 存储子纤程抛出的异常
        std::exception_ptr eptr = nullptr;
        /// \brief 子纤程是否结束
        FiberStatus status = FiberStatus::unstarted;
        /// \brief 真子纤程入口，第一个参数传入纤程对象的引用
        FuncType func;
        /// \brief 纤程信息
#ifdef _WIN32
        PVOID pMainFiber, pNewFiber;
        bool isFormerAThread;
#else
        ucontext_t ctx_main, ctx_fnew;
        const std::unique_ptr<std::array<uint8_t, SIGSTKSZ>> fnew_stack = std::make_unique<std::array<uint8_t, SIGSTKSZ>>();
#endif

    public:
        /** \brief 构造函数
         *
         * 把主线程转化为纤程，并创建一个子纤程
         *
         * \param f 子纤程入口
         */
        Fiber(FuncType f) : func(std::move(f)) {
#ifdef _WIN32
            this->isFormerAThread = !IsThreadAFiber();
            if (this->isFormerAThread) {
                this->pMainFiber = ::ConvertThreadToFiberEx(nullptr, FIBER_FLAG_FLOAT_SWITCH);
            } else {
                this->pMainFiber = ::GetCurrentFiber();
            }
            // default stack size
            this->pNewFiber = ::CreateFiberEx(0, 0, FIBER_FLAG_FLOAT_SWITCH, (void(*)(void *))&fEntry, this);
#else
            ::getcontext(&this->ctx_fnew);
            this->ctx_fnew.uc_stack.ss_sp = this->fnew_stack.get();
            this->ctx_fnew.uc_stack.ss_size = this->fnew_stack->size();
            this->ctx_fnew.uc_link = &this->ctx_main;
            ::makecontext(&this->ctx_fnew, (void(*)())&fEntry, 1, this);
#endif

//        template<typename Fn, typename Args...>
//        Fiber(Fn f,)

        }
        /** \brief 析构函数
         *
         * 删除子纤程，并将主纤程转回线程
         *
         * \warning 主类析构时如子纤程尚未结束（return），则会在子纤程中抛出 FiberReturn 来确保子纤程函数内所有对象都被正确析构
         */
        ~Fiber() noexcept {
            if (!isFinished()) {
                return_();
            }

#ifdef _WIN32
            ::DeleteFiber(this->pNewFiber);
            if (this->isFormerAThread) {
                ::ConvertFiberToThread();
            }
#endif
        }

        /** \brief 调用子纤程
         *
         * 程序流程转入子纤程
         *
         * \warning 子纤程必须尚未结束
         * \return 返回子纤程是否尚未结束
         */
        bool next() {
            assert(!isFinished());
#ifdef _WIN32
            assert(GetCurrentFiber() != this->pNewFiber && "如果你想递归自己，请创建一个新纤程");
            ::SwitchToFiber(this->pNewFiber);
#else
            ::swapcontext(&this->ctx_main, &this->ctx_fnew);
#endif
            if (this->eptr) {
                std::rethrow_exception(std::exchange(this->eptr, nullptr));
            }

            return !isFinished();
        }

        /** \brief 向子纤程内部抛出异常
         *
         * 程序流程转入子纤程，并在子纤程内部抛出异常
         *
         * \param eptr 需抛出异常的指针（可以通过 std::make_exception_ptr 获取）
         * \warning 子纤程必须尚未结束
         * \return 返回子纤程是否尚未结束
         */
        bool throw_(std::exception_ptr&& eptr) {
            assert(!isFinished());
            this->eptr = std::exchange(eptr, nullptr);
            return next();
        }

        /** \brief 强制退出子纤程
         *
         * 向子纤程内部抛出 FiberReturn 异常，以强制退出子纤程，并确保子纤程函数中所有对象都正确析构
         *
         * \warning 子纤程必须尚未结束
         */
        void return_() {
            assert(!isFinished());
            throw_(std::make_exception_ptr(FiberReturn()));
            assert(isFinished() && "请勿吃掉 FiberReturn 异常！！！");
        }

        /** \brief 获得子纤程返回的值
         * \return 子纤程返回的值。如果子纤程没有启动，则返回默认构造值
         */
        YieldValueType current() const {
            return this->yieldedValue;
        }

        /** \brief 判断子纤程是否结束
        * \return 子纤程已经结束(return)返回true，否则false
        */
        bool isFinished() const noexcept {
            return this->status == FiberStatus::closed;
        }

        /** \brief 转回主纤程并输出值
         *
         * \warning 必须由子纤程调用
         *          参数类型必须与子纤程返回值相同，无类型安全
         * \param value 输出到主纤程的值
         */
        void yield(YieldValueType value) {
            assert(!isFinished());
            this->status = FiberStatus::suspended;
            this->yieldedValue = std::move(value);
#ifdef _WIN32
            assert(GetCurrentFiber() != this->pMainFiber && "这虽然是游戏，但绝不是可以随便玩的");
            ::SwitchToFiber(this->pMainFiber);
#else
            ::swapcontext(&this->ctx_fnew, &this->ctx_main);
#endif
            this->status = FiberStatus::running;

            if (this->eptr) {
                std::rethrow_exception(std::exchange(this->eptr, nullptr));
            }
        }

        /** \brief 输出子纤程的所有值
         * \param fiber 另一子纤程
         */
        void yieldAll(Fiber& fiber) {
            assert(&fiber != this);
            while (fiber.next()) {
                this->yield(fiber.current());
            }
        }

        void yieldAll(Fiber&& fiber) {
            this->yieldAll(fiber);
        }

    private:
        /// \brief 子纤程入口的warpper

#ifdef _WIN32
        static void WINAPI fEntry(Fiber *fiber) {
#else
        static void fEntry(Fiber *fiber) {
#endif
            if (!fiber->eptr) {
                fiber->status = FiberStatus::running;
                try {
                    fiber->func(*fiber);
                } catch (FiberReturn &) {
                    // 主 Fiber 对象正在析构
                } catch (...) {
                    fiber->eptr = std::current_exception();
                }
            }
            fiber->status = FiberStatus::closed;
#ifdef _WIN32
            ::SwitchToFiber(fiber->pMainFiber);
#endif
        }
    };

    /** \brief 纤程迭代器类
    *
    * 它通过使用 yield 函数对数组或集合类执行自定义迭代。
    * 用于 C++11 for (... : ...)
    */
    template <typename YieldValueType>
    struct FiberIterator : std::iterator<std::input_iterator_tag, YieldValueType> {
        /// \brief 迭代器尾
        FiberIterator() noexcept : fiber(nullptr) {}
        /** \brief 迭代器首
        * \param _f 主线程类的引用
        */
        FiberIterator(Fiber<YieldValueType>& _f) : fiber(&_f) {
            next();
        }

        /// \brief 转入子纤程
        FiberIterator& operator ++() {
            next();
            return *this;
        }

        /// \brief 取得返回值
        YieldValueType operator *() const {
            assert(fiber != nullptr);
            return std::move(fiber->current());
        }

        /** \brief 比较迭代器相等
        *
        * 通常用于判断迭代是否结束
        * 最好别干别的 ;P
        */
        bool operator ==(const FiberIterator& rhs) const noexcept {
            return fiber == rhs.fiber;
        }
        bool operator !=(const FiberIterator& rhs) const noexcept {
            return !(*this == rhs);
        }

    private:
        void next() {
            assert(fiber);
            if (!fiber->next()) fiber = nullptr;
        }

        Fiber<YieldValueType>* fiber;
    };

    /// \brief 返回迭代器首
    template <typename YieldValueType>
    FiberIterator<YieldValueType> begin(Fiber<YieldValueType>& fiber) {
        return FiberIterator<YieldValueType>(fiber);
    }

    /// \brief 返回迭代器尾
    template <typename YieldValueType>
    FiberIterator<YieldValueType> end(Fiber<YieldValueType>&) noexcept {
        return FiberIterator<YieldValueType>();
    }
}

using namespace std;
using FiberSpace::Fiber;

bool destructedFlag = false;

struct TestDestruct {
    ~TestDestruct() {
        destructedFlag = true;
    }
};

void foo(Fiber<bool>& fiber, int arg) {
    TestDestruct test;
    for (int i = 1; i < 5; i++) {
        printf("goroutine :%d\n", arg+i);
        fiber.yield(false);
    }
}

void permutation(Fiber<array<int, 4>>& fiber, array<int, 4> arr, int length) {
    if (length) {
        for (auto i = 0; i < length; ++i) {
            array<int, 4> newArr(arr);
            std::copy_n(arr.begin(), i, newArr.begin());
            std::copy_n(arr.begin() + i + 1, arr.size() - i - 1, newArr.begin() + i);
            newArr.back() = arr[i];
            fiber.yieldAll(Fiber<array<int, 4>>(bind(permutation, placeholders::_1, newArr, length - 1)));
        }
    } else {
        fiber.yield(arr);
    }
}

int main() {
    {
        Fiber<bool> arg1Fiber(bind(foo, placeholders::_1, 0));
        arg1Fiber.next();
    }
    assert(destructedFlag);

    Fiber<array<int, 4>> fiber(
        bind(permutation, placeholders::_1, array<int, 4> {1, 2, 3, 4}, 4)
    );
    for (auto&& result : fiber) {
        copy(result.begin(), result.end(), std::ostream_iterator<int>(cout, ","));
        cout << endl;
    }
}
