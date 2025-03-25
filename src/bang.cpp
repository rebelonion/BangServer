#include "../include/bang.h"
#include <iostream>
#include <fstream>
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <cstring>

absl::flat_hash_map<std::string_view, std::string_view> BANG_URLS = {};
absl::flat_hash_map<std::string_view, std::string> BANG_DOMAINS = {};
std::vector<Bang> ALL_BANGS;

const std::unordered_map<std::string_view, Category> CATEGORY_MAP = {
    {"Entertainment", Category::Entertainment},
    {"Multimedia", Category::Multimedia},
    {"News", Category::News},
    {"Online Services", Category::OnlineServices},
    {"Research", Category::Research},
    {"Shopping", Category::Shopping},
    {"Tech", Category::Tech},
    {"Translation", Category::Translation}
};

bool loadBangDataFromUrl(const std::string &url) {
    try {
        simdjson::dom::parser parser;

        int sockfd = socket(AF_INET, SOCK_STREAM, 0);
        if (sockfd < 0) {
            std::cerr << "Error creating socket\n";
            return false;
        }

        std::string hostname;
        std::string path;

        size_t hostStart = url.find("://");
        if (hostStart != std::string::npos) {
            hostStart += 3; // Skip "://"
        } else {
            hostStart = 0;
        }

        if (size_t pathStart = url.find('/', hostStart); pathStart != std::string::npos) {
            hostname = url.substr(hostStart, pathStart - hostStart);
            path = url.substr(pathStart);
        } else {
            hostname = url.substr(hostStart);
            path = "/";
        }

        hostent *server = gethostbyname(hostname.c_str());
        if (server == nullptr) {
            std::cerr << "Error: Could not resolve hostname " << hostname << std::endl;
            close(sockfd);
            return false;
        }

        sockaddr_in serverAddr{};
        std::memset(&serverAddr, 0, sizeof(serverAddr));
        serverAddr.sin_family = AF_INET;
        std::memcpy(&serverAddr.sin_addr.s_addr, server->h_addr, server->h_length);
        serverAddr.sin_port = htons(80);

        if (connect(sockfd, reinterpret_cast<sockaddr *>(&serverAddr), sizeof(serverAddr)) < 0) {
            std::cerr << "Error connecting to server\n";
            close(sockfd);
            return false;
        }

        std::string request = "GET " + path + " HTTP/1.1\r\n";
        request += "Host: " + hostname + "\r\n";
        request += "User-Agent: BangServer/1.0\r\n";
        request += "Accept: application/json\r\n";
        request += "Connection: close\r\n\r\n";

        if (send(sockfd, request.c_str(), request.length(), 0) < 0) {
            std::cerr << "Error sending HTTP request\n";
            close(sockfd);
            return false;
        }

        std::string response;
        char buffer[4096];
        ssize_t bytesRead;

        while ((bytesRead = recv(sockfd, buffer, sizeof(buffer) - 1, 0)) > 0) {
            buffer[bytesRead] = '\0';
            response += buffer;
        }

        close(sockfd);

        if (response.empty()) {
            std::cerr << "Error: Empty response from server\n";
            return false;
        }

        size_t bodyStart = response.find("\r\n\r\n");
        if (bodyStart == std::string::npos) {
            std::cerr << "Error: Invalid HTTP response format\n";
            return false;
        }

        std::string json_str = response.substr(bodyStart + 4);

        auto json_result = parser.parse(json_str);
        simdjson::dom::element json;
        auto error = json_result.get(json);
        if (error) {
            std::cerr << "JSON parse error: " << error_message(error) << std::endl;
            return false;
        }

        simdjson::dom::array items;
        error = json.get_array().get(items);
        if (error) {
            std::cerr << "JSON not an array: " << error_message(error) << std::endl;
            return false;
        }

        ALL_BANGS.reserve(items.size());

        for (simdjson::dom::element item: items) {
            auto trigger = std::string("!");

            std::string_view t;
            error = item["t"].get_string().get(t);
            if (error) {
                std::cerr << "Missing required 'trigger' field in bang entry" << std::endl;
                continue;
            }
            trigger += t;

            std::string_view u;
            error = item["u"].get_string().get(u);
            if (error) {
                std::cerr << "Missing required 'url_template' field in bang entry" << std::endl;
                continue;
            }
            std::string url_template(u);

            // Optional fields
            std::optional<Category> category;
            std::string_view c;
            error = item["c"].get_string().get(c);
            if (!error) {
                if (auto it = CATEGORY_MAP.find(c); it != CATEGORY_MAP.end()) {
                    category = it->second;
                }
            }

            std::optional<std::string> domain;
            std::string_view d;
            error = item["d"].get_string().get(d);
            if (!error) {
                domain = std::string(d);
            }

            std::optional<uint64_t> relevance;
            uint64_t r_val;
            error = item["r"].get_uint64().get(r_val);
            if (!error) {
                relevance = r_val;
            } else {
                int64_t r_int;
                error = item["r"].get_int64().get(r_int);
                if (!error) {
                    relevance = static_cast<uint64_t>(r_int);
                }
            }

            std::optional<std::string> short_name;
            std::string_view s;
            error = item["s"].get_string().get(s);
            if (!error) {
                short_name = std::string(s);
            }

            std::optional<std::string> subcategory;
            std::string_view sc;
            error = item["sc"].get_string().get(sc);
            if (!error) {
                subcategory = std::string(sc);
            }

            ALL_BANGS.emplace_back(
                category,
                domain,
                relevance,
                short_name,
                subcategory,
                trigger,
                url_template
            );

            BANG_URLS[ALL_BANGS.back().trigger] = ALL_BANGS.back().url_template;

            if (domain) {
                if (const auto& bang = ALL_BANGS.back(); bang.domain) {
                    std::string domainView = *bang.domain;
                    if (!domainView.starts_with("http")) {
                        domainView.insert(0, "https://");
                    }
                    BANG_DOMAINS[bang.trigger] = std::move(domainView);
                }
            }
        }

        std::cout << "Loaded " << ALL_BANGS.size() << " bang commands" << std::endl;
        return true;
    } catch (const simdjson::simdjson_error &e) {
        std::cerr << "JSON parsing error: " << e.what() << std::endl;
        return false;
    } catch (const std::exception &e) {
        std::cerr << "Error loading bang data: " << e.what() << std::endl;
        return false;
    }
}
