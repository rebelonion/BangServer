#include "../include/bang.h"
#include "../include/http_handler.h"
#include <iostream>
#include <fstream>

absl::flat_hash_map<std::string_view, std::string_view> BANG_URLS = {};
absl::flat_hash_map<std::string_view, std::string> BANG_DOMAINS = {};
std::vector<Bang> ALL_BANGS;

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

bool loadBangDataFromUrl(const std::string &url) {
    try {
        simdjson::dom::parser parser;
        
        std::string json_str = makeHttpRequest(url, "application/json");
        
        if (json_str.empty()) {
            return false;
        }

        auto json_result = parser.parse(json_str);
        simdjson::dom::element json;
        auto error = json_result.get(json);
        if (error) {
            std::cerr << "JSON parse error: " << error_message(error) << std::endl;
            return false;
        }

        simdjson::dom::array items;
        error = json.get_array().get(items);
        if (error) {
            std::cerr << "JSON not an array: " << error_message(error) << std::endl;
            return false;
        }

        ALL_BANGS.reserve(items.size());

        for (simdjson::dom::element item: items) {
            auto trigger = std::string("!");

            std::string_view t;
            error = item["t"].get_string().get(t);
            if (error) {
                std::cerr << "Missing required 'trigger' field in bang entry" << std::endl;
                continue;
            }
            trigger += t;

            std::string_view u;
            error = item["u"].get_string().get(u);
            if (error) {
                std::cerr << "Missing required 'url_template' field in bang entry" << std::endl;
                continue;
            }
            std::string url_template(u);

            // Optional fields
            std::optional<Category> category;
            std::string_view c;
            error = item["c"].get_string().get(c);
            if (!error) {
                if (auto it = CATEGORY_MAP.find(c); it != CATEGORY_MAP.end()) {
                    category = it->second;
                }
            }

            std::optional<std::string> domain;
            std::string_view d;
            error = item["d"].get_string().get(d);
            if (!error) {
                domain = std::string(d);
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

            ALL_BANGS.emplace_back(
                category,
                domain,
                relevance,
                short_name,
                subcategory,
                trigger,
                url_template
            );

            BANG_URLS[ALL_BANGS.back().trigger] = ALL_BANGS.back().url_template;

            if (domain) {
                if (const auto& bang = ALL_BANGS.back(); bang.domain) {
                    std::string domainView = *bang.domain;
                    if (!domainView.starts_with("http")) {
                        domainView.insert(0, "https://");
                    }
                    BANG_DOMAINS[bang.trigger] = std::move(domainView);
                }
            }
        }

        std::cout << "Loaded " << ALL_BANGS.size() << " bang commands" << std::endl;
        return true;
    } catch (const simdjson::simdjson_error &e) {
        std::cerr << "JSON parsing error: " << e.what() << std::endl;
        return false;
    } catch (const std::exception &e) {
        std::cerr << "Error loading bang data: " << e.what() << std::endl;
        return false;
    }
}
