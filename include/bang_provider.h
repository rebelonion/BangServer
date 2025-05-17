#pragma once

#include <string>
#include <memory>
#include <vector>
#include <filesystem>
#include "common_types.h"
#include "bang_source.h"
#include "bang_format.h"

class BangProvider {
public:
    BangProvider(std::unique_ptr<BangSource> source, std::unique_ptr<BangFormat> format);
    [[nodiscard]] std::vector<Bang> loadBangs() const;
    [[nodiscard]] std::string getDescription() const;

private:
    std::unique_ptr<BangSource> source;
    std::unique_ptr<BangFormat> format;
};