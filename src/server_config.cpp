#include "../include/server_config.h"
#include "../include/config_utils.h"
#include <iostream>
#include <cstdlib>
#include <toml.hpp>

void ServerConfig::loadFromFile(const std::string &configPath) {
    loadDefault();

    try {
        auto data = toml::parse(configPath);

        if (data.contains("server")) {
            const auto &server = toml::get<toml::table>(data.at("server"));

            if (server.contains("port")) {
                port = toml::get<int>(server.at("port"));
            }

            if (server.contains("backlog")) {
                backlog = toml::get<int>(server.at("backlog"));
            }

            if (server.contains("queue_depth")) {
                queueDepth = toml::get<size_t>(server.at("queue_depth"));
            }

            if (server.contains("request_buffer_size")) {
                requestBufferSize = toml::get<size_t>(server.at("request_buffer_size"));
            }

            if (server.contains("num_threads")) {
                numThreads = toml::get<int>(server.at("num_threads"));
            }
        }

        if (data.contains("search")) {
            const auto &search = toml::get<toml::table>(data.at("search"));
            if (search.contains("default_url")) {
                defaultSearchUrl = toml::get<std::string>(search.at("default_url"));
            }
            if (search.contains("suggestions_url")) {
                suggestionsUrl = toml::get<std::string>(search.at("suggestions_url"));
            }
        }

        if (data.contains("providers")) {
            for (const auto &providers = toml::get<std::vector<toml::value> >(data.at("providers")); const auto &
                 provider: providers) {
                ProviderConfig providerConfig;

                if (!provider.contains("source")) {
                    std::cerr << "Provider missing 'source' field, skipping" << std::endl;
                    continue;
                }

                if (!provider.contains("format")) {
                    std::cerr << "Provider missing 'format' field, skipping" << std::endl;
                    continue;
                }

                providerConfig.source = toml::get<std::string>(provider.at("source"));
                providerConfig.format = toml::get<std::string>(provider.at("format"));

                if (provider.contains("required")) {
                    providerConfig.required = toml::get<bool>(provider.at("required"));
                }

                addIfMissing(providerConfig);
            }
        }

        if (data.contains("bangs")) {
            ProviderConfig configProviderConfig;
            configProviderConfig.source = configPath;
            configProviderConfig.format = "toml";
            configProviderConfig.required = true;
            addIfMissing(configProviderConfig);
        }

        std::cout << "Loaded configuration from " << configPath << std::endl;
    } catch (const std::exception &e) {
        std::cerr << "Error loading config file: " << e.what() << std::endl;
        std::cerr << "Using default configuration" << std::endl;
    }
}

void ServerConfig::loadDefault() {
    if (const char *port = std::getenv("BANG_PORT")) {
        try {
            this->port = std::stoi(port);
        } catch (...) {
            std::cerr << "Invalid BANG_PORT: " << port << std::endl;
        }
    }

    if (const char *backlog = std::getenv("BANG_BACKLOG")) {
        try {
            this->backlog = std::stoi(backlog);
        } catch (...) {
            std::cerr << "Invalid BANG_BACKLOG: " << backlog << std::endl;
        }
    }

    if (const char *search = std::getenv("BANG_DEFAULT_SEARCH")) {
        this->defaultSearchUrl = search;
    }

    if (const char *threads = std::getenv("BANG_THREADS")) {
        try {
            this->numThreads = std::stoi(threads);
        } catch (...) {
            std::cerr << "Invalid BANG_THREADS: " << threads << std::endl;
        }
    }

    ProviderConfig ddgProvider;
    ddgProvider.source = "https://duckduckgo.com/bang.js";
    ddgProvider.format = "json";
    ddgProvider.required = true;
    this->providers.push_back(ddgProvider);

    if (const char *envPath = std::getenv("BANG_CONFIG_FILE")) {
        ProviderConfig customProvider;
        customProvider.source = envPath;
        customProvider.format = (std::string(envPath).ends_with(".json") ? "json" : "toml");
        this->providers.push_back(customProvider);
    }
}

std::optional<std::filesystem::path> ServerConfig::findConfigFile(const std::string &name) {
    return config::findConfigFile(name);
}

void ServerConfig::addIfMissing(const ProviderConfig &provider) {
    if (std::ranges::find(providers, provider) == providers.end()) {
        providers.push_back(provider);
    }
}
