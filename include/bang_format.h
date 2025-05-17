#pragma once

#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include "common_types.h"

extern const std::unordered_map<std::string_view, Category> CATEGORY_MAP;

class BangFormat {
public:
    virtual ~BangFormat() = default;
    virtual std::vector<Bang> parseContent(const std::string& content) = 0;
    [[nodiscard]] virtual std::string getFormatName() const = 0;
};

class JsonFormat final : public BangFormat {
public:
    std::vector<Bang> parseContent(const std::string& content) override;
    [[nodiscard]] std::string getFormatName() const override;
};

class TomlFormat final : public BangFormat {
public:
    std::vector<Bang> parseContent(const std::string& content) override;
    [[nodiscard]] std::string getFormatName() const override;
};

class BangFormatFactory {
public:
    static std::unique_ptr<BangFormat> createFormat(const std::string& formatHint);
};