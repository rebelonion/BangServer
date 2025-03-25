#include <iostream>
#include <string>
#include <string_view>
#include <chrono>
#include <cstring>
#include <utility>
#include <vector>
#include <thread>

#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <unistd.h>
#include <fcntl.h>
#include <liburing.h>

#include "include/bang.h"
#include "include/memory_pool.h"
#include "include/url_processing.h"

constexpr int PORT = 3000;
constexpr int BACKLOG = 5;

constexpr size_t QUEUE_DEPTH = 256;
constexpr size_t REQUEST_BUFFER_SIZE = 4096;
constexpr char HTTP_SPACE = ' ';
constexpr char HTTP_NL = '\n';
constexpr char HTTP_CR = '\r';

enum class ConnectionState {
    ACCEPT,
    READ,
    PROCESS,
    WRITE,
    CLOSE
};

struct RequestContext {
    int clientFd;
    ConnectionState state;

    char *requestBuffer;
    char *decodeBuffer;
    char *encodeBuffer;
    char *responseBuffer;

    size_t bytesRead;
    size_t responseLen;

    RequestContext()
        : clientFd(-1),
          state(ConnectionState::ACCEPT),
          requestBuffer(getRequestPool().acquire()),
          decodeBuffer(getRequestPool().acquire()),
          encodeBuffer(getEncodePool().acquire()),
          responseBuffer(getRedirectPool().acquire()),
          bytesRead(0),
          responseLen(0) {
    }

    ~RequestContext() {
        if (requestBuffer) getRequestPool().release(requestBuffer);
        if (decodeBuffer) getRequestPool().release(decodeBuffer);
        if (encodeBuffer) getEncodePool().release(encodeBuffer);
        if (responseBuffer) getRedirectPool().release(responseBuffer);
        if (clientFd >= 0) close(clientFd);
    }

    // Allow moving but not copying
    RequestContext(const RequestContext &) = delete;

    RequestContext &operator=(const RequestContext &) = delete;

    RequestContext(RequestContext &&other) noexcept
        : clientFd(other.clientFd),
          state(other.state),
          requestBuffer(other.requestBuffer),
          decodeBuffer(other.decodeBuffer),
          encodeBuffer(other.encodeBuffer),
          responseBuffer(other.responseBuffer),
          bytesRead(other.bytesRead),
          responseLen(other.responseLen) {
        other.clientFd = -1;
        other.requestBuffer = nullptr;
        other.decodeBuffer = nullptr;
        other.encodeBuffer = nullptr;
        other.responseBuffer = nullptr;
    }

    RequestContext &operator=(RequestContext &&other) noexcept {
        if (this != &other) {
            if (requestBuffer) getRequestPool().release(requestBuffer);
            if (decodeBuffer) getRequestPool().release(decodeBuffer);
            if (encodeBuffer) getEncodePool().release(encodeBuffer);
            if (responseBuffer) getRedirectPool().release(responseBuffer);
            if (clientFd >= 0) close(clientFd);

            clientFd = other.clientFd;
            state = other.state;
            requestBuffer = other.requestBuffer;
            decodeBuffer = other.decodeBuffer;
            encodeBuffer = other.encodeBuffer;
            responseBuffer = other.responseBuffer;
            bytesRead = other.bytesRead;
            responseLen = other.responseLen;

            // Reset other
            other.clientFd = -1;
            other.requestBuffer = nullptr;
            other.decodeBuffer = nullptr;
            other.encodeBuffer = nullptr;
            other.responseBuffer = nullptr;
        }
        return *this;
    }
};

void processRequest(RequestContext *ctx) {
    const char *requestStart = ctx->requestBuffer;
    const char *requestEnd = ctx->requestBuffer + ctx->bytesRead;

    // Find first space - after HTTP method
    const char *urlStart = nullptr;
    for (const char *p = requestStart; p < requestEnd - 1; p++) {
        if (*p == HTTP_SPACE) {
            urlStart = p + 1;
            break;
        }
    }

    if (!urlStart) return; // Invalid request

    // Find second space - after URL
    const char *urlEnd = nullptr;
    for (const char *p = urlStart; p < requestEnd - 1; p++) {
        if (*p == HTTP_SPACE) {
            urlEnd = p;
            break;
        }
        // Early termination if we hit the end of line
        if (*p == HTTP_CR && *(p + 1) == HTTP_NL) {
            break;
        }
    }

    if (!urlEnd) return; // Invalid request

    const std::string_view url(urlStart, urlEnd - urlStart);

    auto [searchUrl, encodedQuery] = processQuery(url, ctx->decodeBuffer, ctx->encodeBuffer);
    const auto response = createRedirectResponse(searchUrl, encodedQuery, ctx->responseBuffer);

    ctx->responseLen = response.size();
}

int setupServerSocket() {
    const int serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (serverSocket < 0) {
        std::cerr << "Failed to create socket\n";
        return -1;
    }

    constexpr int opt = 1;
    if (setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        std::cerr << "Failed to set socket options (SO_REUSEADDR)\n";
        close(serverSocket);
        return -1;
    }

    if (setsockopt(serverSocket, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt)) < 0) {
        std::cerr << "Failed to set socket options (TCP_NODELAY)\n";
        close(serverSocket);
        return -1;
    }

    if (fcntl(serverSocket, F_SETFL, fcntl(serverSocket, F_GETFL) | O_NONBLOCK) < 0) {
        std::cerr << "Failed to set non-blocking mode\n";
        close(serverSocket);
        return -1;
    }

    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(PORT);

    if (bind(serverSocket, reinterpret_cast<sockaddr *>(&serverAddr), sizeof(serverAddr)) < 0) {
        std::cerr << "Failed to bind to port " << PORT << "\n";
        close(serverSocket);
        return -1;
    }

    if (listen(serverSocket, BACKLOG) < 0) {
        std::cerr << "Failed to listen on socket\n";
        close(serverSocket);
        return -1;
    }

    return serverSocket;
}

void addAcceptRequest(io_uring *ring, const int serverFd, sockaddr_in *clientAddr, socklen_t *clientAddrLen,
                      RequestContext *ctx) {
    io_uring_sqe *sqe = io_uring_get_sqe(ring);
    io_uring_prep_accept(sqe, serverFd, reinterpret_cast<sockaddr *>(clientAddr), clientAddrLen, 0);
    io_uring_sqe_set_data(sqe, ctx);
}

void addReadRequest(io_uring *ring, RequestContext *ctx) {
    io_uring_sqe *sqe = io_uring_get_sqe(ring);
    io_uring_prep_recv(sqe, ctx->clientFd, ctx->requestBuffer, REQUEST_BUFFER_SIZE - 1, 0);
    io_uring_sqe_set_data(sqe, ctx);
}

void addWriteRequest(io_uring *ring, RequestContext *ctx) {
    io_uring_sqe *sqe = io_uring_get_sqe(ring);
    io_uring_prep_send(sqe, ctx->clientFd, ctx->responseBuffer, ctx->responseLen, 0);
    io_uring_sqe_set_data(sqe, ctx);
}

void addCloseRequest(io_uring *ring, RequestContext *ctx) {
    io_uring_sqe *sqe = io_uring_get_sqe(ring);
    io_uring_prep_nop(sqe);
    io_uring_sqe_set_data(sqe, ctx);
}

int main() {
    std::cout << "Loading bang data from DuckDuckGo API..." << std::endl;
    if (!loadBangDataFromUrl("https://duckduckgo.com/bang.js")) {
        std::cerr << "Failed to load bang data from API\n";
        return 1;
    }
    std::cout << "Successfully loaded " << BANG_URLS.size() << " bang URLs\n";

    const int serverFd = setupServerSocket();
    if (serverFd < 0) {
        return serverFd;
    }

    io_uring ring{};
    io_uring_params params{};
    if (io_uring_queue_init_params(QUEUE_DEPTH, &ring, &params) < 0) {
        std::cerr << "Failed to initialize io_uring\n";
        close(serverFd);
        return 1;
    }

    std::cout << "BangServer starting on http://127.0.0.1:" << PORT << "\n";
    std::cout << "Ready\n";

    sockaddr_in clientAddr{};
    socklen_t clientAddrLen = sizeof(clientAddr);

    std::vector<std::unique_ptr<RequestContext> > contexts;

    auto initialCtx = std::make_unique<RequestContext>();
    addAcceptRequest(&ring, serverFd, &clientAddr, &clientAddrLen, initialCtx.get());
    contexts.push_back(std::move(initialCtx));

    io_uring_submit(&ring);

    // ReSharper disable once CppDFAEndlessLoop
    while (true) {
        io_uring_cqe *cqe;
        if (const int ret = io_uring_wait_cqe(&ring, &cqe); ret < 0) {
            std::cerr << "Error waiting for completion: " << strerror(-ret) << std::endl;
            continue;
        }

        auto *ctx = static_cast<RequestContext *>(io_uring_cqe_get_data(cqe));
        if (!ctx) {
            io_uring_cqe_seen(&ring, cqe);
            continue;
        }

        const int res = cqe->res;
        io_uring_cqe_seen(&ring, cqe);

        if (ctx->state == ConnectionState::ACCEPT) {
            if (res < 0) {
                // Accept failed, submit a new accept
                auto newCtx = std::make_unique<RequestContext>();
                addAcceptRequest(&ring, serverFd, &clientAddr, &clientAddrLen, newCtx.get());
                contexts.push_back(std::move(newCtx));
            } else {
                // Accept succeeded, prepare for read
                ctx->clientFd = res;
                ctx->state = ConnectionState::READ;
                addReadRequest(&ring, ctx);

                auto newCtx = std::make_unique<RequestContext>();
                addAcceptRequest(&ring, serverFd, &clientAddr, &clientAddrLen, newCtx.get());
                contexts.push_back(std::move(newCtx));
            }
        } else if (ctx->state == ConnectionState::READ) {
            if (res <= 0) {
                ctx->state = ConnectionState::CLOSE;
                addCloseRequest(&ring, ctx);
            } else {
                ctx->bytesRead = res;
                ctx->requestBuffer[ctx->bytesRead] = '\0';
                ctx->state = ConnectionState::PROCESS;

                processRequest(ctx);

                ctx->state = ConnectionState::WRITE;
                addWriteRequest(&ring, ctx);
            }
        } else if (ctx->state == ConnectionState::WRITE) {
            ctx->state = ConnectionState::CLOSE;
            addCloseRequest(&ring, ctx);
        } else if (ctx->state == ConnectionState::CLOSE) {
            for (auto it = contexts.begin(); it != contexts.end(); ++it) {
                if (it->get() == ctx) {
                    contexts.erase(it);
                    break;
                }
            }
        }

        io_uring_submit(&ring);
    }

    //io_uring_queue_exit(&ring);
    //close(serverFd);
    //return 0;
}
