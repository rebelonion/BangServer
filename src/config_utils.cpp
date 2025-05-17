#include "../include/config_utils.h"
#include <cstdlib>

namespace config {
    std::vector<std::filesystem::path> getConfigDirectories() {
        std::vector<std::filesystem::path> configDirs;

        configDirs.push_back(std::filesystem::current_path());

#ifdef _WIN32
    // Windows
    if (const char* appData = std::getenv("APPDATA")) {
        configDirs.push_back(std::filesystem::path(appData) / "BangServer");
    }

    if (const char* localAppData = std::getenv("LOCALAPPDATA")) {
        configDirs.push_back(std::filesystem::path(localAppData) / "BangServer");
    }
#elif defined(__APPLE__)
    // macOS
    if (const char* home = std::getenv("HOME")) {
        configDirs.push_back(std::filesystem::path(home) / "Library" / "Application Support" / "BangServer");
        configDirs.push_back(std::filesystem::path(home) / ".config" / "bangserver");
    }
#else
        // Linux and others
        if (const char *xdgConfig = std::getenv("XDG_CONFIG_HOME")) {
            configDirs.push_back(std::filesystem::path(xdgConfig) / "bangserver");
        } else if (const char *home = std::getenv("HOME")) {
            configDirs.push_back(std::filesystem::path(home) / ".config" / "bangserver");
        }

        // System-wide config
        configDirs.emplace_back("/etc/bangserver");
#endif

        return configDirs;
    }

    std::optional<std::filesystem::path> findConfigFile(const std::string &name) {
        for (const auto &dir: getConfigDirectories()) {
            if (auto path = dir / name; std::filesystem::exists(path)) {
                return path;
            }
        }

        return std::nullopt;
    }
}
