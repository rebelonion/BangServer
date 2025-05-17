#include "../include/bang_provider.h"
#include "../include/bang_format.h"
#include "../include/bang_manager.h"
#include <stdexcept>

BangProvider::BangProvider(std::unique_ptr<BangSource> source, std::unique_ptr<BangFormat> format)
    : source(std::move(source)), format(std::move(format)) {}

std::vector<Bang> BangProvider::loadBangs() const {
    try {
        const std::string content = source->fetchContent();
        return format->parseContent(content);
    } catch (const std::exception& e) {
        throw std::runtime_error("Failed to load bangs: " + std::string(e.what()));
    }
}

std::string BangProvider::getDescription() const {
    return format->getFormatName() + " from " + source->getSourceName();
}