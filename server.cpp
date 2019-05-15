// https://blog.csdn.net/ruizeng88/article/details/6682028
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <cerrno>
#include <netinet/in.h>
#include <cstdio>
#include <string>
#include <cstring>
#include <string_view>
#include <liburing.h>
#include "yield.hpp"

#define BUF_LEN 1024
#define SERVER_PORT 8080

using namespace FiberSpace;
using namespace std::literals;

#define DEFINE_URING_OP(operation) \
template <unsigned int N, typename FiberType> \
int await_io_uring_ ## operation (FiberType& fiber, io_uring* ring, int fd, iovec (&&ioves) [N], off_t offset = 0) { \
    auto* sqe = io_uring_get_sqe(ring); \
    io_uring_prep_ ## operation (sqe, fd, ioves, N, offset); \
    io_uring_sqe_set_data(sqe, &fiber); \
    io_uring_submit(ring); \
    fiber.yield(); \
    return fiber.current().value(); \
}

DEFINE_URING_OP(readv)
DEFINE_URING_OP(writev)

inline iovec to_iov(std::string_view sv, size_t len = size_t(-1)) {
    return {
        .iov_base = const_cast<char *>(sv.data()),
        .iov_len = len == size_t(-1) ? sv.length() : len,
    };
}

// 定义好的html页面，实际情况下web server基本是从本地文件系统读取html文件
static const auto http_error_hdr = "HTTP/1.1 404 Not Found\r\nContent-type: text/html\r\n\r\n"sv;
static const auto http_html_hdr = "HTTP/1.1 200 OK\r\nContent-type: text/html\r\n\r\n"sv;
static const auto http_index_html =
"<!doctype html><meta charset='utf-8'><title>Congrats!</title>"
"<h1>Welcome to our HTTP server demo!</h1>"
"<p>This is a just small test page.</p>\n"sv;

// 解析到HTTP请求的文件后，发送本地文件系统中的文件
// 这里，我们处理对index文件的请求，发送我们预定好的html文件
// 呵呵，一切从简！
void http_send_file(Fiber<int, true>& fiber, io_uring* ring, std::string_view filename, int sockfd) {
    int res;
    if (filename == "/") {
        // 通过write函数发送http响应报文；报文包括HTTP响应头和响应内容--HTML文件
        res = await_io_uring_writev(fiber, ring, sockfd, {
            to_iov(http_html_hdr),
            to_iov(http_index_html),
        });
    } else {
        // 文件未找到情况下发送404error响应
        std::printf("%.*s: file not find!\n", int(filename.size()), filename.data());
        res = await_io_uring_writev(fiber, ring, sockfd, { to_iov(http_error_hdr) });
    }

    if (res <= 0) {
        perror("writev");
        if (errno == EAGAIN) {
            return http_send_file(fiber, ring, filename, sockfd); // tail call
        }
    }
}

// HTTP请求解析
int serve(Fiber<int, true>& fiber, io_uring* ring, int sockfd) {
    char buf[BUF_LEN];

retry:
    int res = await_io_uring_readv(fiber, ring, sockfd, { to_iov(buf, sizeof (buf)) });
    if (res <= 0) {
        perror("readv");
        if (errno == EAGAIN) {
            goto retry;
        } else {
            return sockfd;
        }
    }

    std::string_view buf_view(buf, size_t(res));

    if (buf_view.compare(0, 3, "GET"sv) == 0) {
        auto file = buf_view.substr(4, buf_view.find(' ', 4) - 4);
        std::printf("received request: %.*s\n", int(file.size()), file.data());
        http_send_file(fiber, ring, file, sockfd);
    } else {
        // 其他HTTP请求处理，如POST，HEAD等 。这里我们只处理GET
        std::printf("unsupported request: %.*s\n", int(buf_view.size()), buf_view.data());
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
        .sin_zero = {}, // Avoid compiler warning
    };
    if (bind(sockfd, reinterpret_cast<sockaddr *>(&addr), sizeof (sockaddr_in))) {
        std::perror("socket binding failed!\n");
        return 1;
    }
    if (listen(sockfd, 128)) {
        std::perror("listen failed!\n");
        return 1;
    }
    for (;;) {
        // 不间断接收HTTP请求并处理
        if (int newfd = accept(sockfd, nullptr, nullptr); newfd >= 0) {
            // 开始新协程处理请求
            auto* fiber = new Fiber<int, true>(serve, &ring, newfd);
            fiber->next();
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
                if (!fiber->next(cqe->res)) {
                    close(fiber->current().value());
                    delete fiber;
                }
            }
        }
    }
}
