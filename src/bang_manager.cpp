#include "../include/bang_manager.h"
#include "../include/bang_provider.h"
#include "../include/bang_provider_factory.h"
#include <iostream>

absl::flat_hash_map<std::string, Bang> ALL_BANGS = {};

BangManager::BangManager(const ServerConfig &config): config(config) {
    providers = BangProviderFactory::createProvidersFromConfig(config.providers);
}

void BangManager::loadAllBangs() {
    for (const auto& provider : providers) {
        try {
            std::cout << "Loading bangs from " << provider->getDescription() << "..." << std::endl;
            
            std::vector<Bang> loadedBangs = provider->loadBangs();
            std::cout << "  Found " << loadedBangs.size() << " bang commands" << std::endl;
            
            for (auto& bang : loadedBangs) {
                bangs[bang.trigger] = std::move(bang);
            }
            
            std::cout << "Successfully loaded bangs from " << provider->getDescription() << std::endl;
        } catch (const std::exception& e) {
            std::cerr << "Error loading bangs from " << provider->getDescription() 
                      << ": " << e.what() << std::endl;
        }
    }
    
    std::cout << "Total loaded bangs: " << bangs.size() << std::endl;
}

const absl::flat_hash_map<std::string, Bang>& BangManager::getAllBangs() const {
    return bangs;
}