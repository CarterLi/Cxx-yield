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
#include <liburing.h>   // http://git.kernel.dk/liburing
#include <fmt/format.h> // https://github.com/fmtlib/fmt

#include "yield.hpp"

#define BUF_LEN 1024
#define SERVER_PORT 8080

using namespace FiberSpace;
using namespace std::literals;

// 用coroutine包装异步操作
#define DEFINE_URING_OP(operation) \
template <unsigned int N, typename FiberType> \
int await_io_uring_ ## operation (FiberType& fiber, io_uring* ring, int fd, iovec (&&ioves) [N], off_t offset = 0) { \
    auto* sqe = io_uring_get_sqe(ring); \
    io_uring_prep_ ## operation (sqe, fd, ioves, N, offset); \
    io_uring_sqe_set_data(sqe, &fiber); \
    io_uring_submit(ring); \
    fiber.yield(); \
    int res = fiber.current().value(); \
    if (res <= 0) throw std::system_error(errno, std::generic_category()); \
    return res; \
}

DEFINE_URING_OP(readv)
DEFINE_URING_OP(writev)

// 用 string_view（string, char *）填充 iovec 结构体
inline iovec to_iov(std::string_view sv, size_t len = size_t(-1)) {
    return {
        .iov_base = const_cast<char *>(sv.data()),
        .iov_len = len == size_t(-1) ? sv.length() : len,
    };
}

// 一些预定义的错误返回体
static const auto http_404_hdr = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n"sv;
static const auto http_400_hdr = "HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\n\r\n"sv;

// 解析到HTTP请求的文件后，发送本地文件系统中的文件
void http_send_file(Fiber<int, true>& fiber, io_uring* ring, const std::string& filename, int sockfd) {
    // 尝试打开待发送文件
    const auto infd = open(filename.c_str(), O_RDONLY);
    struct stat st;

    if (infd < 0 || fstat(infd, &st) < 0|| !S_ISREG(st.st_mode)) {
        // 文件未找到情况下发送404 error响应
        fmt::print("{}: file not found!\n", filename);
        await_io_uring_writev(fiber, ring, sockfd, { to_iov(http_404_hdr) });
        close(infd);
    } else {
        // 读取文件内容
        std::string filebuf(uint64_t(st.st_size), '\0');
        auto iov = to_iov(filebuf);

        await_io_uring_readv(fiber, ring, infd, { iov });
        close(infd);

        // 发送文件内容
        await_io_uring_writev(fiber, ring, sockfd, {
            to_iov(fmt::format("HTTP/1.1 200 OK\r\nContent-type: text/plain\r\nContent-Length: {}\r\n\r\n", st.st_size)),
            iov,
        });
    }
}

// HTTP请求解析
int serve(Fiber<int, true>& fiber, io_uring* ring, int sockfd) {
    char buf[BUF_LEN];

    int res = await_io_uring_readv(fiber, ring, sockfd, { to_iov(buf, sizeof (buf)) });

    std::string_view buf_view(buf, size_t(res));

    // 这里我们只处理GET请求
    if (buf_view.compare(0, 3, "GET") == 0) {
        // 获取请求的path
        auto file = std::string(buf_view.substr(4, buf_view.find(' ', 4) - 4));
        fmt::print("received request: {}\n", file);
        http_send_file(fiber, ring, file, sockfd);
    } else {
        // 其他HTTP请求处理，如POST，HEAD等，返回400错误
        fmt::print("unsupported request: %.*s\n", buf_view);
        await_io_uring_writev(fiber, ring, sockfd, { to_iov(http_400_hdr) });
    }
    return sockfd;
}

int main() {
    io_uring ring;
    if (io_uring_queue_init(32, &ring, 0) < 0) {
        std::perror("queue_init");
        return 1;
    }

    // 建立TCP套接字
    int sockfd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (sockfd < 0) {
        std::perror("socket creation failed!\n");
        return 1;
    }

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
    if (bind(sockfd, reinterpret_cast<sockaddr *>(&addr), sizeof (sockaddr_in))) {
        std::perror("socket binding failed!\n");
        return 1;
    }
    if (listen(sockfd, 128)) {
        std::perror("listen failed!\n");
        return 1;
    }
    fmt::print("Listening: {}\n", SERVER_PORT);
    const auto cleanFiber = [] (Fiber<int, true>* fiber) {
        close(fiber->current().value());
        delete fiber;
    };
    for (;;) {
        // 不间断接收HTTP请求并处理
        if (int newfd = accept(sockfd, nullptr, nullptr); newfd >= 0) {
            // 开始新协程处理请求
            auto* fiber = new Fiber<int, true>(serve, &ring, newfd);
            if (!fiber->next()) cleanFiber(fiber);
        } else {
            // 获取已完成的IO事件
            io_uring_cqe* cqe;
            if (io_uring_peek_cqe(&ring, &cqe) < 0) {
                std::perror("peek_cqe");
                return -1;
            }
            if (cqe) {
                auto* fiber = static_cast<Fiber<int, true> *>(io_uring_cqe_get_data(cqe));
                io_uring_cqe_seen(&ring, cqe);
                if (fiber && !fiber->next(cqe->res)) cleanFiber(fiber);
            }
        }
    }
}
