#pragma once

#include <string>
#include <string_view>

enum class HttpStatus {
    OK = 200,
    FOUND = 302,
    NOT_FOUND = 404
};

constexpr std::string_view CONTENT_TYPE_HTML = "text/html";
constexpr std::string_view CONTENT_TYPE_XML = "application/opensearchdescription+xml";
constexpr std::string_view CONTENT_TYPE_JSON = "application/json";

extern const std::string_view HOME_PAGE_HTML;
extern const std::string_view OPENSEARCH_XML;

std::string_view createHttpResponse(HttpStatus status, std::string_view contentType, 
                                   std::string_view body, char* buffer);

std::string_view createRedirectResponse(std::string_view searchUrl, std::string_view encodedQuery, char* buffer);

std::string_view extractPath(std::string_view requestData);

std::string makeHttpRequest(const std::string &url, const std::string &acceptType = "application/json");