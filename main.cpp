#include <iostream>
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

#include "include/bang_manager.h"
#include "include/bang_provider.h"
#include "include/bang_provider_factory.h"
#include "include/memory_pool.h"
#include "include/url_processing.h"
#include "include/http_handler.h"
#include "include/server_config.h"

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

    PoolBuffer requestBuffer;
    PoolBuffer decodeBuffer;
    PoolBuffer encodeBuffer;
    PoolBuffer responseBuffer;

    size_t bytesRead;
    size_t responseLen;

    RequestContext()
        : clientFd(-1),
          state(ConnectionState::ACCEPT),
          requestBuffer(getRequestPool()),
          decodeBuffer(getRequestPool()),
          encodeBuffer(getEncodePool()),
          responseBuffer(getRedirectPool()),
          bytesRead(0),
          responseLen(0) {
    }

    ~RequestContext() {
        if (clientFd >= 0) close(clientFd);
    }

    RequestContext(const RequestContext &) = delete;
    RequestContext &operator=(const RequestContext &) = delete;

    RequestContext(RequestContext &&other) noexcept
        : clientFd(other.clientFd),
          state(other.state),
          requestBuffer(std::move(other.requestBuffer)),
          decodeBuffer(std::move(other.decodeBuffer)),
          encodeBuffer(std::move(other.encodeBuffer)),
          responseBuffer(std::move(other.responseBuffer)),
          bytesRead(other.bytesRead),
          responseLen(other.responseLen) {
        other.clientFd = -1;
    }

    RequestContext &operator=(RequestContext &&other) noexcept {
        if (this != &other) {
            if (clientFd >= 0) close(clientFd);

            clientFd = other.clientFd;
            state = other.state;
            requestBuffer = std::move(other.requestBuffer);
            decodeBuffer = std::move(other.decodeBuffer);
            encodeBuffer = std::move(other.encodeBuffer);
            responseBuffer = std::move(other.responseBuffer);
            bytesRead = other.bytesRead;
            responseLen = other.responseLen;

            other.clientFd = -1;
        }
        return *this;
    }
};

void processRequest(RequestContext *ctx) {
    const char *requestStart = ctx->requestBuffer.get();
    const char *requestEnd = ctx->requestBuffer.get() + ctx->bytesRead;

    const std::string_view requestStr(requestStart, ctx->bytesRead);

    if (const std::string_view path = extractPath(requestStr); path == "/") {
        if (requestStr.find("?q=") != std::string_view::npos) {
            const std::string_view url(requestStart, requestEnd - requestStart);
            auto [searchUrl, encodedQuery] = processQuery(url, ctx->decodeBuffer.get(), ctx->encodeBuffer.get());
            if (searchUrl.empty() && encodedQuery.empty()) {
                ctx->responseLen = createHttpResponse(HttpStatus::REQUEST_TOO_LARGE, CONTENT_TYPE_HTML, "",
                                                      ctx->responseBuffer.get()).size();
                return;
            }
            ctx->responseLen = createRedirectResponse(searchUrl, encodedQuery, ctx->responseBuffer.get()).size();
            return;
        }
        // Serve home page
        ctx->responseLen = createHttpResponse(HttpStatus::OK, CONTENT_TYPE_HTML, HOME_PAGE_HTML, ctx->responseBuffer.get()).
                size();
    } else if (path == "/opensearch.xml") {
        // Serve OpenSearch XML
        ctx->responseLen = createHttpResponse(HttpStatus::OK, CONTENT_TYPE_XML, OPENSEARCH_XML_CONTENT, ctx->responseBuffer.get()).
                size();
    } else {
        // For any other path, process as potential search query
        const std::string_view url(requestStart, requestEnd - requestStart);
        auto [searchUrl, encodedQuery] = processQuery(url, ctx->decodeBuffer.get(), ctx->encodeBuffer.get());
        ctx->responseLen = createRedirectResponse(searchUrl, encodedQuery, ctx->responseBuffer.get()).size();
    }
}

int setupServerSocket(const ServerConfig &config) {
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
    serverAddr.sin_port = htons(config.port);

    if (bind(serverSocket, reinterpret_cast<sockaddr *>(&serverAddr), sizeof(serverAddr)) < 0) {
        std::cerr << "Failed to bind to port " << config.port << "\n";
        close(serverSocket);
        return -1;
    }

    if (listen(serverSocket, config.backlog) < 0) {
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

void addReadRequest(io_uring *ring, RequestContext *ctx, size_t bufferSize) {
    io_uring_sqe *sqe = io_uring_get_sqe(ring);
    io_uring_prep_recv(sqe, ctx->clientFd, ctx->requestBuffer.get(), bufferSize - 1, 0);
    io_uring_sqe_set_data(sqe, ctx);
}

void addWriteRequest(io_uring *ring, RequestContext *ctx) {
    io_uring_sqe *sqe = io_uring_get_sqe(ring);
    io_uring_prep_send(sqe, ctx->clientFd, ctx->responseBuffer.get(), ctx->responseLen, 0);
    io_uring_sqe_set_data(sqe, ctx);
}

void addCloseRequest(io_uring *ring, RequestContext *ctx) {
    io_uring_sqe *sqe = io_uring_get_sqe(ring);
    io_uring_prep_nop(sqe);
    io_uring_sqe_set_data(sqe, ctx);
}

int main() {
    std::cout << "Loading configuration..." << std::endl;
    ServerConfig config;

    if (const auto configPath = ServerConfig::findConfigFile("bangserver.toml")) {
        config.loadFromFile(configPath->string());
    } else {
        config.loadDefault();
    }

    DEFAULT_SEARCH_URL = config.defaultSearchUrl;
    OPENSEARCH_XML_CONTENT = generateOpenSearchXml(config);

    std::cout << "Initializing BangManager..." << std::endl;
    BangManager bangManager(config);
    bangManager.loadAllBangs();

    ALL_BANGS = bangManager.getAllBangs();

    const int serverFd = setupServerSocket(config);
    if (serverFd < 0) {
        return serverFd;
    }

    io_uring ring{};
    io_uring_params params{};
    if (io_uring_queue_init_params(config.queueDepth, &ring, &params) < 0) {
        std::cerr << "Failed to initialize io_uring\n";
        close(serverFd);
        return 1;
    }

    std::cout << "BangServer starting on http://127.0.0.1:" << config.port << "\n";
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
                addReadRequest(&ring, ctx, config.requestBufferSize);

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
                ctx->requestBuffer.get()[ctx->bytesRead] = '\0';
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
