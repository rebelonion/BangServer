#pragma once

#include <vector>
#include <filesystem>
#include <string>
#include <optional>

namespace config {
    std::vector<std::filesystem::path> getConfigDirectories();

    std::optional<std::filesystem::path> findConfigFile(const std::string& name);
}