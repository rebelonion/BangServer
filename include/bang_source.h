#pragma once

#include <string>
#include <memory>
#include <filesystem>


class BangSource {
public:
    virtual ~BangSource() = default;
    virtual std::string fetchContent() = 0;
    [[nodiscard]] virtual std::string getSourceName() const = 0;
};

class HttpSource final : public BangSource {
public:
    explicit HttpSource(std::string url);
    std::string fetchContent() override;
    [[nodiscard]] std::string getSourceName() const override;

private:
    std::string url;
};

class FileSource final : public BangSource {
public:
    explicit FileSource(std::filesystem::path filePath);
    std::string fetchContent() override;
    [[nodiscard]] std::string getSourceName() const override;

private:
    std::filesystem::path filePath;
};

class BangSourceFactory {
public:
    static std::unique_ptr<BangSource> createSource(const std::string& source);
};