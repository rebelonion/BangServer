#include "../include/http_handler.h"
#include "../include/url_processing.h"
#include <cstring>
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <iostream>

const std::string_view HOME_PAGE_HTML = R"(<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>BangServer</title>
    <link rel="search" type="application/opensearchdescription+xml" title="BangSearch" href="/opensearch.xml" />
    <style>
        body { font-family: system-ui, -apple-system, sans-serif; max-width: 800px; margin: 0 auto; padding: 20px; }
        h1 { color: #333; }
        code { background: #f5f5f5; padding: 2px 4px; border-radius: 4px; }
    </style>
</head>
<body>
    <h1>BangServer</h1>
    <p>High-performance C++ server for DuckDuckGo-style bang commands.</p>
    <p>Add to your browser by clicking the address bar options (or right-clicking the search field) and selecting "Add BangSearch".</p>
    <p>Use <code>!</code> followed by a keyword to search on specific sites, e.g. <code>!w cats</code> for Wikipedia.</p>
</body>
</html>)";

const std::string_view OPENSEARCH_XML = R"(<?xml version="1.0" encoding="UTF-8"?>
<OpenSearchDescription xmlns="http://a9.com/-/spec/opensearch/1.1/">
  <ShortName>BangSearch</ShortName>
  <Description>Fast bang search</Description>
  <InputEncoding>UTF-8</InputEncoding>
  <Url type="text/html" method="GET" template="http://localhost:3000/?q={searchTerms}"/>
  <Url type="application/x-suggestions+json" method="GET" template="https://search.brave.com/api/suggest?q={searchTerms}"/>
</OpenSearchDescription>)";

std::string_view createHttpResponse(const HttpStatus status, const std::string_view contentType,
                                    const std::string_view body, char *buffer) {
    char *ptr = buffer;
    std::string statusLine;

    switch (status) {
        case HttpStatus::OK:
            statusLine = "HTTP/1.1 200 OK\r\n";
            break;
        case HttpStatus::FOUND:
            statusLine = "HTTP/1.1 302 Found\r\n";
            break;
        case HttpStatus::NOT_FOUND:
            statusLine = "HTTP/1.1 404 Not Found\r\n";
            break;
        default:
            statusLine = "HTTP/1.1 200 OK\r\n";
    }

    // Write status line
    const size_t statusLineLen = statusLine.size();
    memcpy(ptr, statusLine.data(), statusLineLen);
    ptr += statusLineLen;

    // Write Content-Type header
    constexpr std::string_view contentTypeHeader = "Content-Type: ";
    memcpy(ptr, contentTypeHeader.data(), contentTypeHeader.size());
    ptr += contentTypeHeader.size();
    memcpy(ptr, contentType.data(), contentType.size());
    ptr += contentType.size();
    memcpy(ptr, "\r\n", 2);
    ptr += 2;

    // Write Content-Length header
    constexpr std::string_view contentLengthHeader = "Content-Length: ";
    memcpy(ptr, contentLengthHeader.data(), contentLengthHeader.size());
    ptr += contentLengthHeader.size();

    // Convert body length to string
    char lengthStr[32];
    const int lengthStrLen = snprintf(lengthStr, sizeof(lengthStr), "%zu", body.size());
    memcpy(ptr, lengthStr, lengthStrLen);
    ptr += lengthStrLen;
    memcpy(ptr, "\r\n", 2);
    ptr += 2;

    // End headers
    memcpy(ptr, "Connection: close\r\n\r\n", 21);
    ptr += 21;

    // Write body
    memcpy(ptr, body.data(), body.size());
    ptr += body.size();

    return {buffer, static_cast<std::string_view::size_type>(ptr - buffer)};
}

// Create redirect response (specialized for our use case)
std::string_view createRedirectResponse(const std::string_view searchUrl, const std::string_view encodedQuery,
                                        char *buffer) {
    char *ptr = buffer;

    constexpr std::string_view header = "HTTP/1.1 302 Found\r\nLocation: ";
    constexpr std::string_view footer = "\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
    constexpr std::string_view placeholder = "{{{s}}}";
    memcpy(ptr, header.data(), header.size());
    ptr += header.size();

    const auto placeholderPos = static_cast<const char *>(memmem(searchUrl.data(), searchUrl.size(),
                                                                 placeholder.data(), placeholder.size()));

    if (placeholderPos) {
        const size_t beforeLen = placeholderPos - searchUrl.data();
        memcpy(ptr, searchUrl.data(), beforeLen);
        ptr += beforeLen;
        memcpy(ptr, encodedQuery.data(), encodedQuery.size());
        ptr += encodedQuery.size();
        const size_t afterLen = searchUrl.size() - beforeLen - placeholder.size();
        memcpy(ptr, placeholderPos + placeholder.size(), afterLen);
        ptr += afterLen;
    } else {
        // Append encoded query to searchUrl
        memcpy(ptr, searchUrl.data(), searchUrl.size());
        ptr += searchUrl.size();
        memcpy(ptr, encodedQuery.data(), encodedQuery.size());
        ptr += encodedQuery.size();
    }

    // Copy footer
    memcpy(ptr, footer.data(), footer.size());
    ptr += footer.size();

    return {buffer, static_cast<std::string_view::size_type>(ptr - buffer)};
}

std::string_view extractPath(const std::string_view requestData) {
    const char *data = requestData.data();
    const size_t size = requestData.size();

    // Find first space after HTTP method
    const char *urlStart = nullptr;
    for (const char *p = data; p < data + size - 1; p++) {
        if (*p == ' ') {
            urlStart = p + 1;
            break;
        }
    }

    if (!urlStart) return "/"; // Default to root path if no URL found

    // Find second space (after URL)
    const char *urlEnd = nullptr;
    for (const char *p = urlStart; p < data + size - 1; p++) {
        if (*p == ' ') {
            urlEnd = p;
            break;
        }
        // Early termination if we hit the end of line
        if (*p == '\r' && *(p + 1) == '\n') {
            urlEnd = p;
            break;
        }
    }

    if (!urlEnd) return "/"; // Default to root path if URL end not found

    // Check for query string and strip it
    const char *queryStart = nullptr;
    for (const char *p = urlStart; p < urlEnd; p++) {
        if (*p == '?') {
            queryStart = p;
            break;
        }
    }

    if (queryStart) {
        return {urlStart, static_cast<size_t>(queryStart - urlStart)};
    }

    return {urlStart, static_cast<size_t>(urlEnd - urlStart)};
}

// Blocking for now
std::string makeHttpRequest(const std::string &url, const std::string &acceptType) {
    const int socFd = socket(AF_INET, SOCK_STREAM, 0);
    if (socFd < 0) {
        std::cerr << "Error creating socket\n";
        return "";
    }

    std::string hostname;
    std::string path;

    size_t hostStart = url.find("://");
    if (hostStart != std::string::npos) {
        hostStart += 3; // Skip "://"
    } else {
        hostStart = 0;
    }

    if (const size_t pathStart = url.find('/', hostStart); pathStart != std::string::npos) {
        hostname = url.substr(hostStart, pathStart - hostStart);
        path = url.substr(pathStart);
    } else {
        hostname = url.substr(hostStart);
        path = "/";
    }

    // Resolve hostname
    const hostent *server = gethostbyname(hostname.c_str());
    if (server == nullptr) {
        std::cerr << "Error: Could not resolve hostname " << hostname << std::endl;
        close(socFd);
        return "";
    }

    // Set up connection
    sockaddr_in serverAddr{};
    std::memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    std::memcpy(&serverAddr.sin_addr.s_addr, server->h_addr, server->h_length);
    serverAddr.sin_port = htons(80);

    if (connect(socFd, reinterpret_cast<sockaddr *>(&serverAddr), sizeof(serverAddr)) < 0) {
        std::cerr << "Error connecting to server\n";
        close(socFd);
        return "";
    }

    std::string request = "GET " + path + " HTTP/1.1\r\n";
    request += "Host: " + hostname + "\r\n";
    request += "User-Agent: BangServer/1.0\r\n";
    request += "Accept: " + acceptType + "\r\n";
    request += "Connection: close\r\n\r\n";

    if (send(socFd, request.c_str(), request.length(), 0) < 0) {
        std::cerr << "Error sending HTTP request\n";
        close(socFd);
        return "";
    }

    // Receive response
    std::string response;
    char buffer[4096];
    ssize_t bytesRead;

    while ((bytesRead = recv(socFd, buffer, sizeof(buffer) - 1, 0)) > 0) {
        buffer[bytesRead] = '\0';
        response += buffer;
    }

    close(socFd);

    if (response.empty()) {
        std::cerr << "Error: Empty response from server\n";
        return "";
    }

    // Extract response body
    const size_t bodyStart = response.find("\r\n\r\n");
    if (bodyStart == std::string::npos) {
        std::cerr << "Error: Invalid HTTP response format\n";
        return "";
    }

    return response.substr(bodyStart + 4);
}
