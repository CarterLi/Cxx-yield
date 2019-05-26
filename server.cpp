// https://blog.csdn.net/ruizeng88/article/details/6682028
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <cerrno>
#include <cstring>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>
#include <chrono>
#include <liburing.h>   // http://git.kernel.dk/liburing
#include <fmt/format.h> // https://github.com/fmtlib/fmt

#include "yield.hpp"

enum {
    BUF_SIZE = 1024,
    SERVER_PORT = 8080,
    MAX_CONNECTION_NUM = 32,
};

using namespace FiberSpace;
using namespace std::literals;

[[noreturn]]
void panic(std::string_view sv) { // 简单起见，把错误直接转化成异常抛出，终止程序
    throw std::system_error(errno, std::generic_category(), sv.data());
}

using pool_ptr_t = std::vector<std::array<char, BUF_SIZE>>::pointer;

struct fiber_data {
    io_uring* ring;
    pool_ptr_t pool_start_ptr;
    pool_ptr_t pool_ptr;
    std::chrono::time_point<std::chrono::high_resolution_clock> start;
    int clientfd;
};

// 异步读操作，不使用缓冲区
#define DEFINE_URING_OP(operation) \
template <unsigned int N> \
int await_##operation (Fiber<int, fiber_data>& fiber, int fd, iovec (&&ioves) [N], off_t offset = 0) { \
    auto* sqe = io_uring_get_sqe(fiber.localData.ring); \
    assert(sqe && "sqe should not be NULL"); \
    io_uring_prep_##operation (sqe, fd, ioves, N, offset); \
    io_uring_sqe_set_data(sqe, &fiber); \
    io_uring_submit(fiber.localData.ring); \
    fiber.yield(); \
    int res = fiber.current().value(); \
    if (res <= 0) panic(#operation); \
    return res; \
}

DEFINE_URING_OP(readv)
DEFINE_URING_OP(writev)

// 异步读操作，使用缓冲区
#define DEFINE_URING_FIXED_OP(operation) \
int await_##operation##_fixed (Fiber<int, fiber_data>& fiber, int fd, size_t nbyte = 0, off_t offset = 0) { \
    auto* sqe = io_uring_get_sqe(fiber.localData.ring); \
    assert(sqe && "sqe should not be NULL"); \
    auto* pool = fiber.localData.pool_ptr; \
    if (!nbyte) nbyte = pool->size(); \
    io_uring_prep_##operation##_fixed (sqe, fd, pool, uint32_t(nbyte), offset); \
    sqe->buf_index = uint16_t(pool - fiber.localData.pool_start_ptr); \
    io_uring_sqe_set_data(sqe, &fiber); \
    io_uring_submit(fiber.localData.ring); \
    fiber.yield(); \
    int res = fiber.current().value(); \
    if (res <= 0) panic(#operation "_fixed"); \
    return res; \
}

DEFINE_URING_FIXED_OP(read)
DEFINE_URING_FIXED_OP(write)

// 把控制权交给其他协程
int yield_execution(Fiber<int, fiber_data>& fiber) {
    auto* sqe = io_uring_get_sqe(fiber.localData.ring);
    assert(sqe && "sqe should not be NULL"); \
    io_uring_prep_nop(sqe);
    io_uring_sqe_set_data(sqe, &fiber);
    io_uring_submit(fiber.localData.ring);
    fiber.yield();
    return fiber.current().value();
}

// 判断循环队列 io_uring 是否已满
bool is_uring_full(io_uring* ring) {
    // Copied from: io_uring_get_sqe
    io_uring_sq *sq = &ring->sq;
    unsigned next = sq->sqe_tail + 1;

    return next - sq->sqe_head > *sq->kring_entries;
}

// 填充 iovec 结构体
constexpr inline iovec to_iov(char *buf, size_t size) {
    return {
        .iov_base = buf,
        .iov_len = size,
    };
}
constexpr inline iovec to_iov(std::string_view sv) {
    return to_iov(const_cast<char *>(sv.data()), sv.size());
}
template <size_t N>
constexpr inline iovec to_iov(std::array<char, N>& array) {
    return to_iov(array.data(), array.size());
}

template <typename Fn>
struct on_scope_exit {
    on_scope_exit(Fn &&fn): _fn(std::move(fn)) {}
    ~on_scope_exit() { this->_fn(); }

private:
    Fn _fn;
};

// 一些预定义的错误返回体
static const auto http_404_hdr = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n"sv;
static const auto http_400_hdr = "HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\n\r\n"sv;

// 解析到HTTP请求的文件后，发送本地文件系统中的文件
void http_send_file(Fiber<int, fiber_data>& fiber, const std::string& filename) {
    const auto sockfd = fiber.localData.clientfd;
    // 尝试打开待发送文件
    const auto infd = open(filename.c_str(), O_RDONLY);
    on_scope_exit closefd([=]() { close(infd); });

    if (struct stat st; infd < 0 || fstat(infd, &st) || !S_ISREG(st.st_mode)) {
        // 文件未找到情况下发送404 error响应
        fmt::print("{}: file not found!\n", filename);
        await_writev(fiber, sockfd, { to_iov(http_404_hdr) });
    } else {
        // 发送响应头
        await_writev(fiber, sockfd, {
            to_iov(fmt::format("HTTP/1.1 200 OK\r\nContent-type: text/plain\r\nContent-Length: {}\r\n\r\n", st.st_size)),
        });
        off_t offset = 0;
        if (fiber.localData.pool_ptr) {
            // 一次读取 BUF_SIZE 个字节数据并发送
            for (; st.st_size - offset > BUF_SIZE; offset += BUF_SIZE) {
                await_read_fixed(fiber, infd, BUF_SIZE, offset);
                await_write_fixed(fiber, sockfd, BUF_SIZE);

                // for (int i = 0; i < 100000; ++i) {
                //     yield_execution(fiber);
                // }
            }
            // 读取剩余数据并发送
            if (st.st_size > offset) {
                await_read_fixed(fiber, infd, size_t(st.st_size - offset), offset);
                await_write_fixed(fiber, sockfd, size_t(st.st_size - offset));
            }
        } else {
            std::array<char, BUF_SIZE> filebuf;
            auto iov = to_iov(filebuf);
            for (; st.st_size - offset > BUF_SIZE; offset += BUF_SIZE) {
                await_readv(fiber, infd, { iov }, offset);
                await_writev(fiber, sockfd, { iov });

                // for (int i = 0; i < 100000; ++i) {
                //     yield_execution(fiber);
                // }
            }
            if (st.st_size > offset) {
                iov.iov_len = size_t(st.st_size - offset);
                await_readv(fiber, infd, { iov }, offset);
                await_writev(fiber, sockfd, { iov });
            }
        }
    }
}

// HTTP请求解析
void serve(Fiber<int, fiber_data>& fiber) {
    const auto sockfd = fiber.localData.clientfd;

    std::string_view buf_view;
    std::array<char, BUF_SIZE> buffer;
    // 优先使用缓存池
    if (fiber.localData.pool_ptr) {
        // 缓冲区可用时直接加载数据至缓冲区内
        int res = await_read_fixed(fiber, sockfd);
        buf_view = std::string_view(fiber.localData.pool_ptr->data(), size_t(res));
    } else {
        // 不可用则另找内存加载数据
        int res = await_readv(fiber, sockfd, { to_iov(buffer) });
        buf_view = std::string_view(buffer.data(), size_t(res));
    }

    // 这里我们只处理GET请求
    if (buf_view.compare(0, 3, "GET") == 0) {
        // 获取请求的path
        auto file = std::string(buf_view.substr(4, buf_view.find(' ', 4) - 4));
        fmt::print("received request {} with sockfd {}\n", file, sockfd);
        http_send_file(fiber, file);
    } else {
        // 其他HTTP请求处理，如POST，HEAD等，返回400错误
        fmt::print("unsupported request: {}\n", buf_view);
        await_writev(fiber, sockfd, { to_iov(http_400_hdr) });
    }
}

int main() {
    // 初始化IO循环队列，内核支持的原生异步操作实现
    io_uring ring;
    if (io_uring_queue_init(MAX_CONNECTION_NUM, &ring, 0)) panic("queue_init");
    on_scope_exit closerg([&]() { io_uring_queue_exit(&ring); });

    // 开辟缓冲区池，用以减少与内核通信所需内存映射次数
    std::array<iovec, 12> iov_pool; // 经测至多12个，多了报 EFAULT
    std::vector<std::array<char, BUF_SIZE>> buffers(iov_pool.size());
    // 用一个集合保存可用的缓冲区
    std::vector<pool_ptr_t> available_buffers;
    available_buffers.reserve(iov_pool.size());
    for (size_t i = 0; i < buffers.size(); ++i) {
        iov_pool[i] = to_iov(buffers[i]);
        available_buffers.push_back(&buffers[i]);
    }
    // 注册缓冲区
    if (io_uring_register(ring.ring_fd, IORING_REGISTER_BUFFERS, iov_pool.data(), iov_pool.size())) panic("register_buffer");

    // 建立TCP套接字
    int sockfd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (sockfd < 0) panic("socket creation");
    on_scope_exit closesock([=]() { close(sockfd); });

    sockaddr_in addr = {
        .sin_family = AF_INET,
        // 这里要注意，端口号一定要使用htons先转化为网络字节序，否则绑定的实际端口可能和你需要的不同
        .sin_port = htons(SERVER_PORT),
        .sin_addr = {
            .s_addr = INADDR_ANY,
        },
        .sin_zero = {}, // 消除编译器警告
    };
    // 绑定端口
    if (bind(sockfd, reinterpret_cast<sockaddr *>(&addr), sizeof (sockaddr_in))) panic("socket binding");
    // 监听端口
    if (listen(sockfd, 128)) panic("listen");
    fmt::print("Listening: {}\n", SERVER_PORT);

    const auto cleanFiber = [&] (Fiber<int, fiber_data>* fiber) {
        // 请求结束时清理资源
        close(fiber->localData.clientfd);
        fmt::print("sockfd {} is closed, time used {}, with pool {}\n",
            fiber->localData.clientfd,
            (std::chrono::high_resolution_clock::now() - fiber->localData.start).count(),
            (void *)fiber->localData.pool_ptr);
        if (fiber->localData.pool_ptr) {
            // 将缓冲区重新放入可用列表
            available_buffers.push_back(fiber->localData.pool_ptr);
        }
        delete fiber;
    };

    static volatile sig_atomic_t exit_flag = 0;

    // （不那么）妥善的处理 Ctrl+C
    struct sigaction sigHandler = {};
    sigHandler.sa_handler = [](int) { exit_flag = 1; };
    if (sigaction(SIGINT, &sigHandler, nullptr)) panic("sigaction");

    while (!exit_flag) {
        // 获取已完成的IO事件
        io_uring_cqe* cqe;
        // 当判断缓冲区列表已满时，阻塞等待IO事件，以确保sqe总是可用
        if (!is_uring_full(&ring) ? io_uring_peek_cqe(&ring, &cqe) : io_uring_wait_cqe(&ring, &cqe)) panic("peek/wait_cqe");

        if (cqe) {
            // 有已完成的事件，回到协程继续
            auto* fiber = static_cast<Fiber<int, fiber_data> *>(io_uring_cqe_get_data(cqe));
            io_uring_cqe_seen(&ring, cqe);
            if (fiber && !fiber->next(cqe->res)) cleanFiber(fiber);
        } else {
            // 空闲时才尝试接收HTTP请求，避免开启太多协程，同时提升性能
            if (int newfd = accept(sockfd, nullptr, nullptr); newfd >= 0) {
                // 新建新协程处理请求
                auto* fiber = new Fiber<int, fiber_data>(serve);
                // 注册必要数据，这个数据对整个协程都可用
                fiber->localData = {
                    .ring = &ring,
                    .pool_start_ptr = buffers.data(),
                    .pool_ptr = [&]() {
                        pool_ptr_t result = nullptr;
                        if (!available_buffers.empty()) {
                            // 如果池中有可用的缓冲区，使用之
                            result = available_buffers.back();
                            available_buffers.pop_back();
                        }
                        return result;
                    }(),
                    .start = std::chrono::high_resolution_clock::now(),
                    .clientfd = newfd,
                };
                fmt::print("Accepting connection, sockfd {} with pool: {}\n", newfd, (void *)fiber->localData.pool_ptr);
                // 进入协程开始处理请求
                if (!fiber->next()) cleanFiber(fiber);
            }
        }
    }

    fmt::print("\nBye.\n");
}
