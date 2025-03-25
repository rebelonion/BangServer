#pragma once

#include <string>
#include <string_view>
#include <optional>
#include <unordered_map>
#include <vector>
#include <absl/container/flat_hash_map.h>

#include "simdjson.h"

enum class Category {
    Entertainment,
    Multimedia,
    News,
    OnlineServices,
    Research,
    Shopping,
    Tech,
    Translation
};

struct Bang {
    std::optional<Category> category;
    std::optional<std::string> domain;
    std::optional<uint64_t> relevance;
    std::optional<std::string> short_name;
    std::optional<std::string> subcategory;
    std::string trigger;
    std::string url_template;

    Bang(std::string t, std::string u)
        : trigger(std::move(t)), url_template(std::move(u)) {
    }

    Bang(
        const std::optional<Category> c,
        std::optional<std::string> d,
        const std::optional<uint64_t> r,
        std::optional<std::string> s,
        std::optional<std::string> sc,
        std::string t,
        std::string u
    ) : category(c), domain(std::move(d)), relevance(r), short_name(std::move(s)),
        subcategory(std::move(sc)), trigger(std::move(t)), url_template(std::move(u)) {
    }
};

extern absl::flat_hash_map<std::string_view, std::string_view> BANG_URLS;
extern absl::flat_hash_map<std::string_view, std::string> BANG_DOMAINS;
extern std::vector<Bang> ALL_BANGS;
extern const std::unordered_map<std::string_view, Category> CATEGORY_MAP;

bool loadBangDataFromUrl(const std::string &url);
