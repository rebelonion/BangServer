#pragma once

#include <memory>
#include <vector>
#include <filesystem>
#include <optional>
#include "bang_provider.h"
#include "server_config.h"

class BangProviderFactory {
public:
    static std::vector<std::unique_ptr<BangProvider>> createProvidersFromConfig(const std::vector<ProviderConfig> &providerConfigs);
    
private:
    static std::vector<std::filesystem::path> getConfigDirectories();
    static std::optional<std::filesystem::path> findConfigFile(const std::string& name);
};