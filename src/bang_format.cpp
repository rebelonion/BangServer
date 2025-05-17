#include "../include/bang_format.h"
#include "../include/bang_manager.h"
#include "../include/simdjson.h"
#include <toml.hpp>
#include <iostream>
#include <stdexcept>
#include <algorithm>
#include <cctype>

const std::unordered_map<std::string_view, Category> CATEGORY_MAP = {
    {"Entertainment", Category::Entertainment},
    {"Multimedia", Category::Multimedia},
    {"News", Category::News},
    {"Online Services", Category::OnlineServices},
    {"Research", Category::Research},
    {"Shopping", Category::Shopping},
    {"Tech", Category::Tech},
    {"Translation", Category::Translation}
};

static std::optional<Category> getCategoryFromString(const std::string &categoryStr) {
    if (const auto it = CATEGORY_MAP.find(categoryStr); it != CATEGORY_MAP.end()) {
        return it->second;
    }

    return std::nullopt;
}

std::vector<Bang> JsonFormat::parseContent(const std::string &content) {
    std::vector<Bang> results;

    try {
        simdjson::dom::parser parser;
        auto jsonResult = parser.parse(content);
        simdjson::dom::element json;
        auto error = jsonResult.get(json);

        if (error) {
            throw std::runtime_error("JSON parse error: " + std::string(error_message(error)));
        }

        simdjson::dom::array items;
        error = json.get_array().get(items);

        if (error) {
            throw std::runtime_error("JSON is not an array: " + std::string(error_message(error)));
        }

        for (simdjson::dom::element item: items) {
            auto trigger = std::string("!");

            std::string_view t;
            error = item["t"].get_string().get(t);
            if (error) {
                std::cerr << "Missing required 'trigger' field in bang entry" << std::endl;
                continue;
            }
            std::string trigger_value(t);
            trigger += trigger_value;

            std::string_view u;
            error = item["u"].get_string().get(u);
            if (error) {
                std::cerr << "Missing required 'url_template' field in bang entry" << std::endl;
                continue;
            }
            auto url_template = std::string(u);

            // Optional fields
            std::optional<Category> category;
            std::string_view c;
            error = item["c"].get_string().get(c);
            if (!error) {
                category = getCategoryFromString(std::string(c));
            }

            std::optional<std::string> domain;
            std::string_view d;
            error = item["d"].get_string().get(d);
            if (!error) {
                domain = std::string(d);
            }

            if (domain && !domain->starts_with("http")) {
                domain->insert(0, "https://");
            }

            std::optional<uint64_t> relevance;
            uint64_t r_val;
            error = item["r"].get_uint64().get(r_val);
            if (!error) {
                relevance = r_val;
            } else {
                int64_t r_int;
                error = item["r"].get_int64().get(r_int);
                if (!error) {
                    relevance = static_cast<uint64_t>(r_int);
                }
            }

            std::optional<std::string> short_name;
            std::string_view s;
            error = item["s"].get_string().get(s);
            if (!error) {
                short_name = std::string(s);
            }

            std::optional<std::string> subcategory;
            std::string_view sc;
            error = item["sc"].get_string().get(sc);
            if (!error) {
                subcategory = std::string(sc);
            }

            Bang bang(
                category,
                domain,
                relevance,
                short_name,
                subcategory,
                trigger,
                url_template
            );

            results.push_back(std::move(bang));
        }
    } catch (const std::exception &e) {
        throw std::runtime_error("Error parsing JSON: " + std::string(e.what()));
    }

    return results;
}

std::string JsonFormat::getFormatName() const {
    return "JSON";
}

std::vector<Bang> TomlFormat::parseContent(const std::string &content) {
    std::vector<Bang> results;

    try {
        auto data = toml::parse_str(content);

        if (!data.contains("bangs")) {
            throw std::runtime_error("TOML file must contain a 'bangs' array");
        }

        for (const auto &bangsArray = toml::get<std::vector<toml::value> >(data.at("bangs")); const auto &bangTable:
             bangsArray) {
            // Required fields
            if (!bangTable.contains("trigger")) {
                std::cerr << "Missing required 'trigger' field in bang entry" << std::endl;
                continue;
            }

            if (!bangTable.contains("url_template")) {
                std::cerr << "Missing required 'url_template' field in bang entry" << std::endl;
                continue;
            }

            const std::string trigger = "!" + toml::get<std::string>(bangTable.at("trigger"));
            const std::string urlTemplate = toml::get<std::string>(bangTable.at("url_template"));

            // Optional fields
            std::optional<Category> category;
            if (bangTable.contains("category")) {
                const std::string categoryStr = toml::get<std::string>(bangTable.at("category"));
                category = getCategoryFromString(categoryStr);
            }

            std::optional<std::string> domain;
            if (bangTable.contains("domain")) {
                domain = toml::get<std::string>(bangTable.at("domain"));

                if (!domain->starts_with("http")) {
                    domain->insert(0, "https://");
                }
            }

            std::optional<uint64_t> relevance;
            if (bangTable.contains("relevance")) {
                relevance = toml::get<uint64_t>(bangTable.at("relevance"));
            }

            std::optional<std::string> shortName;
            if (bangTable.contains("short_name")) {
                shortName = toml::get<std::string>(bangTable.at("short_name"));
            }

            std::optional<std::string> subcategory;
            if (bangTable.contains("subcategory")) {
                subcategory = toml::get<std::string>(bangTable.at("subcategory"));
            }

            Bang bang(
                category,
                domain,
                relevance,
                shortName,
                subcategory,
                trigger,
                urlTemplate
            );

            results.push_back(std::move(bang));
        }
    } catch (const std::exception &e) {
        throw std::runtime_error("Error parsing TOML: " + std::string(e.what()));
    }

    return results;
}

std::string TomlFormat::getFormatName() const {
    return "TOML";
}

std::unique_ptr<BangFormat> BangFormatFactory::createFormat(const std::string &formatHint) {
    std::string lowerHint = formatHint;
    std::ranges::transform(lowerHint, lowerHint.begin(),
                           [](const unsigned char c) { return std::tolower(c); });

    if (lowerHint == "json" || lowerHint.ends_with(".json")) {
        return std::make_unique<JsonFormat>();
    }
    if (lowerHint == "toml" || lowerHint.ends_with(".toml")) {
        return std::make_unique<TomlFormat>();
    }

    std::cerr << "Unknown format: " << formatHint << ", defaulting to JSON" << std::endl;
    return std::make_unique<JsonFormat>();
}
