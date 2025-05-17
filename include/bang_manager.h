#pragma once

#include <memory>
#include <vector>
#include "common_types.h"
#include "server_config.h"

class BangProvider;

class BangManager {
public:
    explicit BangManager(const ServerConfig &config);
    void loadAllBangs();
    [[nodiscard]] const absl::flat_hash_map<std::string, Bang>& getAllBangs() const;
    
private:
    absl::flat_hash_map<std::string, Bang> bangs;
    std::vector<std::unique_ptr<BangProvider>> providers;
    const ServerConfig &config;
};