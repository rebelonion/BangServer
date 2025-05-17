#include "../include/bang_provider_factory.h"
#include "../include/bang_format.h"
#include "../include/bang_source.h"
#include "../include/bang_manager.h"
#include "../include/config_utils.h"
#include <iostream>
#include <fstream>
#include <format>

std::vector<std::unique_ptr<BangProvider>> BangProviderFactory::createProvidersFromConfig(const std::vector<ProviderConfig> &providerConfigs) {
    std::vector<std::unique_ptr<BangProvider>> providers;

    if (!providerConfigs.empty()) {
        for (const auto&[source, format, required] : providerConfigs) {
            try {
                auto sourceObj = BangSourceFactory::createSource(source);
                auto formatObj = BangFormatFactory::createFormat(format);
                
                providers.push_back(std::make_unique<BangProvider>(
                    std::move(sourceObj),
                    std::move(formatObj)
                ));
            } catch (const std::exception& e) {
                if (required) {
                    throw std::runtime_error(std::format("Failed to create required provider ({}, {}): {}",
                                                        source, format, e.what()));
                }
                std::cerr << "Skipping optional provider due to error: " << e.what() << std::endl;
            }
        }

        if (!providers.empty()) {
            return providers;
        }
    }

    std::cerr << "No providers configured, falling back to searching for bang config files" << std::endl;
    
    providers.push_back(std::make_unique<BangProvider>(
        BangSourceFactory::createSource("https://duckduckgo.com/bang.js"),
        BangFormatFactory::createFormat("json")
    ));
    
    for (const auto &dir: config::getConfigDirectories()) {
        for (const auto &ext: {".json", ".toml"}) {
            for (const auto &filename: {"bangs", "bang"}) {
                if (auto path = dir / (filename + std::string(ext)); std::filesystem::exists(path)) {
                    providers.push_back(std::make_unique<BangProvider>(
                        BangSourceFactory::createSource(path.string()),
                        BangFormatFactory::createFormat(ext)
                    ));
                }
            }
        }
    }
    
    return providers;
}

std::vector<std::filesystem::path> BangProviderFactory::getConfigDirectories() {
    return config::getConfigDirectories();
}

std::optional<std::filesystem::path> BangProviderFactory::findConfigFile(const std::string &name) {
    return config::findConfigFile(name);
}
