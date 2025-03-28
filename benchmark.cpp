#include <iostream>
#include <string>
#include <string_view>
#include <chrono>
#include <vector>
#include <utility>
#include <random>
#include <thread>
#include <future>
#include <atomic>
#include <cerrno>

#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/time.h>

#include "include/bang.h"
#include "include/memory_pool.h"
#include "include/url_processing.h"
#include "include/http_handler.h"

std::string generateRandomQuery(const bool includeBang, std::mt19937 &rng) {
    static const std::vector<std::string> bangs = {
        "!g", "!w", "!yt", "!gh", "!so", "!maps", "!reddit", "!news", "!images", "!translate"
    };

    static const std::vector<std::string> queryWords = {
        "programming", "c++", "performance", "optimization", "algorithm",
        "data structure", "network", "server", "benchmark", "latency",
        "throughput", "parsing", "string", "url", "encoding", "decoding",
        "concurrent", "parallel", "async", "memory", "cache", "compiler"
    };

    std::string query;

    if (includeBang) {
        std::uniform_int_distribution<size_t> bangDist(0, bangs.size() - 1);
        query = bangs[bangDist(rng)] + " ";
    }

    // Add 1-5 random words
    std::uniform_int_distribution<size_t> wordCountDist(1, 5);
    std::uniform_int_distribution<size_t> wordDist(0, queryWords.size() - 1);

    const size_t wordCount = wordCountDist(rng);
    for (size_t i = 0; i < wordCount; ++i) {
        if (i > 0) query += " ";
        query += queryWords[wordDist(rng)];
    }

    return query;
}

std::string prepareRequestUrl(const std::string &query) {
    std::string result = "/search?q=";

    for (const char c: query) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
            c == '!' || c == '-' || c == '_' || c == '.' || c == '~') {
            result += c;
        } else if (c == ' ') {
            result += '+';
        } else {
            result += '%';
            result += "0123456789ABCDEF"[(c >> 4) & 0xF];
            result += "0123456789ABCDEF"[c & 0xF];
        }
    }
    return result;
}

int createClientSocket(const std::string &serverAddress, int port) {
    const int sockFd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockFd < 0) {
        std::cerr << "Error creating socket" << std::endl;
        return -1;
    }

    struct sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(port);

    if (inet_pton(AF_INET, serverAddress.c_str(), &serverAddr.sin_addr) <= 0) {
        std::cerr << "Invalid address or address not supported" << std::endl;
        close(sockFd);
        return -1;
    }

    // Set TCP_NODELAY
    constexpr int flag = 1;
    if (setsockopt(sockFd, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(int)) < 0) {
        std::cerr << "Failed to set TCP_NODELAY" << std::endl;
        close(sockFd);
        return -1;
    }

    if (connect(sockFd, reinterpret_cast<sockaddr *>(&serverAddr), sizeof(serverAddr)) < 0) {
        std::cerr << "Connection failed" << std::endl;
        close(sockFd);
        return -1;
    }

    return sockFd;
}

bool sendHttpRequest(const int sockFd, const std::string &url) {
    const std::string request = "GET " + url + " HTTP/1.1\r\n"
                                "Host: localhost\r\n"
                                "Connection: close\r\n\r\n";

    const ssize_t sent = send(sockFd, request.c_str(), request.size(), 0);
    return sent == static_cast<ssize_t>(request.size());
}

std::string receiveHttpResponse(const int sockFd) {
    char buffer[4096];
    std::string response;

    timeval tv{};
    tv.tv_sec = 1; // 1 second timeout
    tv.tv_usec = 0;
    setsockopt(sockFd, SOL_SOCKET, SO_RCVTIMEO, (const char *) &tv, sizeof tv);

    while (true) {
        const ssize_t bytesRead = recv(sockFd, buffer, sizeof(buffer) - 1, 0);

        if (bytesRead < 0) {
            // Error or timeout
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // Timeout occurred, which means we're done if we have data
                if (!response.empty()) {
                    break;
                }
                return ""; // Empty response with timeout, consider failed
            }
            return ""; // Error
        }

        if (bytesRead == 0) {
            // Connection closed by server
            break;
        }

        buffer[bytesRead] = '\0';
        response.append(buffer, bytesRead);

        // If we've received the end of the headers (double CRLF)
        if (response.find("\r\n\r\n") != std::string::npos) {
            // For a 302 redirect with content-length 0, we're done
            if (response.find("HTTP/1.1 302") != std::string::npos &&
                response.find("Content-Length: 0") != std::string::npos) {
                break;
            }
        }
    }

    return response;
}

void clientWorker(
    const std::vector<std::string> &urls,
    const int startIdx,
    const int endIdx,
    const std::string &serverAddress,
    const int port,
    std::atomic<int> &successCount,
    std::atomic<int> &failCount
) {
    for (int i = startIdx; i < endIdx; ++i) {
        const int sockFd = createClientSocket(serverAddress, port);
        if (sockFd < 0) {
            ++failCount;
            continue;
        }

        if (!sendHttpRequest(sockFd, urls[i])) {
            ++failCount;
            close(sockFd);
            continue;
        }

        std::string response = receiveHttpResponse(sockFd);
        close(sockFd);

        if (response.empty() || response.find("HTTP/1.1 302") == std::string::npos) {
            ++failCount;
        } else {
            ++successCount;
        }
    }
}

void runNetworkBenchmark(const std::vector<std::string> &allTestUrls, const std::string &serverAddress, int port,
                         int numThreads = -1) {
    std::vector<std::string> testUrls;

    if (constexpr size_t networkQueryCount = 10000; allTestUrls.size() > networkQueryCount) {
        testUrls.reserve(networkQueryCount);
        const size_t step = allTestUrls.size() / networkQueryCount;
        for (size_t i = 0; i < networkQueryCount; ++i) {
            testUrls.push_back(allTestUrls[i * step]);
        }
    } else {
        testUrls = allTestUrls;
    }

    std::cout << "=============== NETWORK BENCHMARK ===============" << std::endl;
    std::cout << "Running network benchmark with " << testUrls.size() << " queries..." << std::endl;
    std::cout << "Connecting to server at " << serverAddress << ":" << port << std::endl;

    // If numThreads is -1 (default), use 1 thread
    // If numThreads is 0, use all available hardware threads
    if (numThreads == 0) {
        numThreads = static_cast<int>(std::thread::hardware_concurrency());
    } else if (numThreads < 0) {
        numThreads = 1;
    }

    std::cout << "Using " << numThreads << " threads for benchmark" << std::endl;

    constexpr int numRuns = 3;
    constexpr int warmupRuns = 1;

    const unsigned queriesPerThread = testUrls.size() / numThreads;

    for (int run = 0; run < warmupRuns + numRuns; ++run) {
        std::cout << (run < warmupRuns ? "Warmup" : "Benchmark") << " run " << (run < warmupRuns
            ? run + 1
            : run - warmupRuns + 1) << "..." << std::endl;

        std::atomic successCount(0);
        std::atomic failCount(0);
        std::vector<std::thread> threads;

        auto start = std::chrono::high_resolution_clock::now();

        for (int t = 0; t < numThreads; ++t) {
            unsigned startIdx = t * queriesPerThread;
            unsigned endIdx = (t == numThreads - 1) ? testUrls.size() : (t + 1) * queriesPerThread;

            threads.emplace_back(clientWorker,
                                 std::ref(testUrls),
                                 startIdx,
                                 endIdx,
                                 std::ref(serverAddress),
                                 port,
                                 std::ref(successCount),
                                 std::ref(failCount));
        }

        for (auto &thread: threads) {
            thread.join();
        }

        auto end = std::chrono::high_resolution_clock::now();
        const auto duration = std::chrono::duration<double, std::milli>(end - start).count();

        if (run >= warmupRuns) {
            const double queriesPerSecond = (successCount.load() * 1000.0) / duration;
            const double avgQueryTimeMs = duration / successCount.load();

            std::cout << "Run " << (run - warmupRuns + 1) << " results:" << std::endl;
            std::cout << "  Duration: " << duration << " ms" << std::endl;
            std::cout << "  Successful queries: " << successCount.load() << std::endl;
            std::cout << "  Failed queries: " << failCount.load() << std::endl;
            std::cout << "  Queries per second: " << queriesPerSecond << std::endl;
            if (avgQueryTimeMs < 1.0) {
                const double avgQueryTimeUs = avgQueryTimeMs * 1000.0;
                std::cout << "  Average time per query: " << avgQueryTimeUs << " µs" << std::endl;
            } else {
                std::cout << "  Average time per query: " << avgQueryTimeMs << " ms" << std::endl;
            }
        } else {
            std::cout << "  Warmup complete. Successful: " << successCount.load()
                    << ", Failed: " << failCount.load() << std::endl;
        }
    }
}

void processUrlBatch(
    const std::vector<std::string> &urls,
    const size_t startIdx,
    const size_t endIdx
) {
    char *decodeBuffer = getRequestPool().acquire();
    char *encodeBuffer = getEncodePool().acquire();
    char *responseBuffer = getRedirectPool().acquire();

    for (size_t i = startIdx; i < endIdx; ++i) {
        auto [searchUrl, encodedQuery] = processQuery(urls[i], decodeBuffer, encodeBuffer);
        // Prevent optimization
        if (auto response = createRedirectResponse(searchUrl, encodedQuery, responseBuffer); response.empty()) {
            std::cerr << "Error: empty response\n";
        }
    }

    getRequestPool().release(decodeBuffer);
    getEncodePool().release(encodeBuffer);
    getRedirectPool().release(responseBuffer);
}

void runInProcessBenchmark(const std::vector<std::string> &testUrls, int numThreads = -1) {
    std::cout << "=============== IN-PROCESS BENCHMARK ===============" << std::endl;

    // If numThreads is -1 (default), use 1 thread
    // If numThreads is 0, use all available hardware threads
    if (numThreads == 0) {
        numThreads = static_cast<int>(std::thread::hardware_concurrency());
    } else if (numThreads < 0) {
        numThreads = 1;
    }

    std::cout << "Using " << numThreads << " threads for benchmark" << std::endl;

    // Warm-up phase
    std::cout << "Running warmup..." << std::endl;
    char *decodeBuffer = getRequestPool().acquire();
    char *encodeBuffer = getEncodePool().acquire();
    char *responseBuffer = getRedirectPool().acquire();

    for (int i = 0; i < 1; ++i) {
        for (const auto &url: testUrls) {
            auto [searchUrl, encodedQuery] = processQuery(url, decodeBuffer, encodeBuffer);
            // Prevent optimization
            if (auto response = createRedirectResponse(searchUrl, encodedQuery, responseBuffer); response.empty()) {
                std::cerr << "Error: empty response\n";
            }
        }
    }

    getRequestPool().release(decodeBuffer);
    getEncodePool().release(encodeBuffer);
    getRedirectPool().release(responseBuffer);

    // Benchmark phase
    constexpr int numRuns = 5;
    double totalDuration = 0.0;

    std::cout << "Running benchmark..." << std::endl;

    for (int run = 0; run < numRuns; ++run) {
        auto start = std::chrono::high_resolution_clock::now();

        if (numThreads == 1) {
            char *tDecodeBuffer = getRequestPool().acquire();
            char *tEncodeBuffer = getEncodePool().acquire();
            char *tResponseBuffer = getRedirectPool().acquire();

            for (const auto &url: testUrls) {
                auto [searchUrl, encodedQuery] = processQuery(url, tDecodeBuffer, tEncodeBuffer);
                // Prevent optimization
                if (auto response = createRedirectResponse(searchUrl, encodedQuery, tResponseBuffer); response.
                    empty()) {
                    std::cerr << "Error: empty response\n";
                }
            }

            getRequestPool().release(tDecodeBuffer);
            getEncodePool().release(tEncodeBuffer);
            getRedirectPool().release(tResponseBuffer);
        } else {
            std::vector<std::thread> threads;
            const size_t queriesPerThread = testUrls.size() / numThreads;

            for (int t = 0; t < numThreads; ++t) {
                size_t startIdx = t * queriesPerThread;
                size_t endIdx = (t == numThreads - 1) ? testUrls.size() : (t + 1) * queriesPerThread;

                threads.emplace_back(processUrlBatch,
                                     std::ref(testUrls),
                                     startIdx,
                                     endIdx);
            }

            for (auto &thread: threads) {
                thread.join();
            }
        }

        auto end = std::chrono::high_resolution_clock::now();
        const auto duration = std::chrono::duration<double, std::milli>(end - start).count();
        totalDuration += duration;

        std::cout << "Run " << (run + 1) << ": " << duration << " ms for " << testUrls.size() << " queries" <<
                std::endl;
    }

    const double avgDuration = totalDuration / numRuns;
    const double queriesPerSecond = (static_cast<double>(testUrls.size()) / avgDuration) * 1000.0;
    const double avgQueryTimeUs = (avgDuration / static_cast<double>(testUrls.size())) * 1000.0;

    std::cout << "=========================" << std::endl;
    std::cout << "Average time: " << avgDuration << " ms for " << testUrls.size() << " queries" << std::endl;
    std::cout << "Queries per second: " << queriesPerSecond << std::endl;
    std::cout << "Average time per query: " << avgQueryTimeUs << " µs" << std::endl;
}

int main(const int argc, char *argv[]) {
    std::cout << "Loading bang data from bang.json..." << std::endl;
    if (!loadBangDataFromUrl("https://duckduckgo.com/bang.js")) {
        std::cerr << "Failed to load bang data from API\n";
        return 1;
    }
    std::cout << "Successfully loaded " << ALL_BANGS.size() << " bang URLs\n";

    // RNG
    std::random_device rd;
    std::mt19937 rng(rd());
    std::uniform_int_distribution bangDist(0, 1);

    // 50% with bangs, 50% without
    constexpr size_t numQueries = 1000000;
    std::vector<std::string> testUrls;
    testUrls.reserve(numQueries);

    for (size_t i = 0; i < numQueries; ++i) {
        const bool includeBang = bangDist(rng) == 1;
        std::string query = generateRandomQuery(includeBang, rng);
        testUrls.push_back(prepareRequestUrl(query));
    }

    std::string mode = "in-process";
    std::string serverAddress = "127.0.0.1";
    int port = 3000;
    int threads = -1; // -1 means use 1 thread (default)

    for (int i = 1; i < argc; ++i) {
        if (std::string arg = argv[i]; arg == "--network" || arg == "-n") {
            mode = "network";
        } else if ((arg == "--address" || arg == "-a") && i + 1 < argc) {
            serverAddress = argv[++i];
        } else if ((arg == "--port" || arg == "-p") && i + 1 < argc) {
            port = std::stoi(argv[++i]);
        } else if ((arg == "--threads" || arg == "-t") && i + 1 < argc) {
            threads = std::stoi(argv[++i]);
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "Usage: benchmark [options]\n"
                    << "Options:\n"
                    << "  --network, -n         Run network benchmark (requires running server)\n"
                    << "  --address, -a ADDR    Server address (default: 127.0.0.1)\n"
                    << "  --port, -p PORT       Server port (default: 3000)\n"
                    << "  --threads, -t THREADS Number of threads for benchmark (default: 1, 0 = all available)\n"
                    << "  --help, -h            Show this help message\n";
            return 0;
        }
    }

    if (mode == "network") {
        runNetworkBenchmark(testUrls, serverAddress, port, threads);
    } else {
        runInProcessBenchmark(testUrls, threads);
    }

    return 0;
}
