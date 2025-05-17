#pragma once

#include <string>
#include <vector>
#include <optional>
#include <filesystem>

struct ProviderConfig {
    std::string source;
    std::string format;
    bool required = false;

    bool operator!=(const ProviderConfig &other) const {
        return source != other.source; // No config should have the same source but different format
    }

    bool operator==(const ProviderConfig &other) const {
        return source == other.source;
    }
};

struct ServerConfig {
public:
    // Network settings
    int port = 3000;
    int backlog = 5;
    size_t queueDepth = 256;
    size_t requestBufferSize = 4096;

    // Default search settings
    std::string defaultSearchUrl = "https://www.google.com/search?q=";

    // Performance settings
    std::optional<int> numThreads;

    // Bang providers
    std::vector<ProviderConfig> providers;

    void loadFromFile(const std::string &configPath);

    void loadDefault();

    static std::optional<std::filesystem::path> findConfigFile(const std::string &name);

private:
    void addIfMissing(const ProviderConfig &provider);
};
