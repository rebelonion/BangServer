#include "../include/bang_source.h"
#include "../include/http_handler.h"
#include "../include/bang_manager.h"
#include <fstream>
#include <sstream>
#include <stdexcept>

HttpSource::HttpSource(std::string url) : url(std::move(url)) {}

std::string HttpSource::fetchContent() {
    try {
        std::string content = makeHttpRequest(url, "application/json");
        if (content.empty()) {
            throw std::runtime_error("Failed to fetch data from URL: " + url);
        }
        return content;
    } catch (const std::exception& e) {
        throw std::runtime_error("HTTP request failed: " + std::string(e.what()));
    }
}

std::string HttpSource::getSourceName() const {
    return "URL: " + url;
}

FileSource::FileSource(std::filesystem::path filePath) : filePath(std::move(filePath)) {}

std::string FileSource::fetchContent() {
    if (!std::filesystem::exists(filePath)) {
        throw std::runtime_error("File not found: " + filePath.string());
    }
    
    std::ifstream file(filePath);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open file: " + filePath.string());
    }
    
    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string content = buffer.str();
    
    if (content.empty()) {
        throw std::runtime_error("File is empty: " + filePath.string());
    }
    
    return content;
}

std::string FileSource::getSourceName() const {
    return "File: " + filePath.string();
}

std::unique_ptr<BangSource> BangSourceFactory::createSource(const std::string& source) {
    if (source.starts_with("http://") || source.starts_with("https://")) {
        return std::make_unique<HttpSource>(source);
    }
    return std::make_unique<FileSource>(std::filesystem::path(source));
}
