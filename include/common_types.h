#pragma once

#include <string>
#include <optional>
#include <absl/container/flat_hash_map.h>

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

    Bang() = default;

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

extern absl::flat_hash_map<std::string, Bang> ALL_BANGS;