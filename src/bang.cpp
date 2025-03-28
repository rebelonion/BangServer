#include "../include/bang.h"
#include "../include/http_handler.h"
#include <iostream>
#include <fstream>
#include <cstdlib>
#include <filesystem>

absl::flat_hash_map<std::string, Bang> ALL_BANGS = {};

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

std::string getCustomBangsFilePath() {
    if (const char *envPath = std::getenv("BANG_CONFIG_FILE")) {
        return envPath;
    }

    return "bangs.json";
}

int processBangJsonArray(const simdjson::dom::array &items, bool isOverride) {
    try {
        size_t addedCount = 0;
        simdjson::error_code error;

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

            ALL_BANGS[trigger] = std::move(bang);
            
            if (isOverride) {
                std::cout << "Overridden bang command: " << trigger << "\n";
            }

            addedCount++;
        }

        return addedCount;
    } catch (const std::exception &e) {
        std::cerr << "Error processing bang data: " << e.what() << std::endl;
        return false;
    }
}

bool loadBangDataFromUrl(const std::string &url) {
    try {
        simdjson::dom::parser parser;

        const std::string json_str = makeHttpRequest(url, "application/json");

        if (json_str.empty()) {
            return false;
        }

        const auto json_result = parser.parse(json_str);
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

        if (const int added = processBangJsonArray(items, false); added > 0) {
            std::cout << "Loaded " << added << " bang commands from URL" << std::endl;
            return true;
        }

        return false;
    } catch (const simdjson::simdjson_error &e) {
        std::cerr << "JSON parsing error: " << e.what() << std::endl;
        return false;
    } catch (const std::exception &e) {
        std::cerr << "Error loading bang data from URL: " << e.what() << std::endl;
        return false;
    }
}

bool loadBangDataFromFile(const std::string &filePath) {
    try {
        if (!std::filesystem::exists(filePath)) {
            std::cout << "Custom bangs file not found at: " << filePath << std::endl;
            return false;
        }

        std::ifstream file(filePath);
        if (!file.is_open()) {
            std::cerr << "Failed to open custom bangs file: " << filePath << std::endl;
            return false;
        }

        std::string json_str((std::istreambuf_iterator(file)), std::istreambuf_iterator<char>());
        file.close();

        if (json_str.empty()) {
            std::cerr << "Custom bangs file is empty: " << filePath << std::endl;
            return false;
        }

        simdjson::dom::parser parser;
        auto json_result = parser.parse(json_str);
        simdjson::dom::element json;
        auto error = json_result.get(json);
        if (error) {
            std::cerr << "JSON parse error in custom bangs file: " << error_message(error) << std::endl;
            return false;
        }

        simdjson::dom::array items;
        error = json.get_array().get(items);
        if (error) {
            std::cerr << "Custom bangs file must contain a JSON array: " << error_message(error) << std::endl;
            return false;
        }

        if (const int added = processBangJsonArray(items, true); added > 0) {
            std::cout << "Loaded " << added << " custom bang commands from: " << filePath <<
                    std::endl;
            return true;
        }

        return false;
    } catch (const simdjson::simdjson_error &e) {
        std::cerr << "JSON parsing error in custom bangs file: " << e.what() << std::endl;
        return false;
    } catch (const std::exception &e) {
        std::cerr << "Error loading custom bang data from file: " << e.what() << std::endl;
        return false;
    }
}
