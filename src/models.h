#pragma once
// ───────── 模型发现：从 /v1/models 拉取列表、方向键菜单选择，并把结果写回 config ─────────

#include <algorithm>
#include <optional>
#include <string>
#include <vector>

#include "../include/cpp-httplib/httplib.h"
#include "../include/json/json.hpp"
#include "spdlog/spdlog.h"

#include "config.h"
#include "lineedit.h"

using json = nlohmann::json;

// ───────── 日志：统一走 openai logger，未注册时退回默认 logger ─────────
inline void log_models_error(const std::string& msg) {
    auto logger = spdlog::get("openai");
    if (logger) logger->error("{}", msg);
    else spdlog::error("{}", msg);
}

// ───────── 拉取模型：GET /v1/models，兼容 data[].id 与 models[].name，去重后返回 ─────────

inline std::string clean_base_url() {
    std::string url = base_url;
    while (!url.empty() && url.back() == '/') url.pop_back();
    if (url.size() > 3 && url.compare(url.size() - 3, 3, "/v1") == 0) {
        url.erase(url.size() - 3);
    }
    return url;
}

inline std::optional<std::vector<std::string>> fetch_models() {
    if (base_url.empty()) {
        log_models_error("fetch_models: base_url is not set");
        return std::nullopt;
    }

    httplib::Client cli(base_url);
    cli.set_connection_timeout(10);
    cli.set_read_timeout(30);

    httplib::Headers headers = {
        {"Authorization", std::string("Bearer ") + api_key},
        {"Content-Type", "application/json"}
    };

    auto res = cli.Get("/v1/models", headers);
    if (!res) {
        log_models_error("GET /v1/models failed: cannot connect to " + base_url);
        std::cout << "Cannot connect to " << clean_base_url() << "\n";
        return std::nullopt;
    }
    if (res->status != 200) {
        log_models_error("GET /v1/models failed: HTTP " + std::to_string(res->status) +
                         " - " + res->body);
        std::cout << "Endpoint returned HTTP " << res->status << "\n";
        return std::nullopt;
    }

    std::vector<std::string> result;
    try {
        json j = json::parse(res->body);
        if (j.is_object() && j.contains("data") && j["data"].is_array()) {
            for (const auto& item : j["data"]) {
                if (item.is_string()) {
                    result.push_back(item.get<std::string>());
                } else if (item.is_object() && item.contains("id") && item["id"].is_string()) {
                    result.push_back(item["id"].get<std::string>());
                }
            }
        } else if (j.is_object() && j.contains("models") && j["models"].is_array()) {
            for (const auto& item : j["models"]) {
                if (item.is_string()) result.push_back(item.get<std::string>());
                else if (item.is_object() && item.contains("name") && item["name"].is_string())
                    result.push_back(item["name"].get<std::string>());
            }
        }
    } catch (const std::exception& e) {
        log_models_error(std::string("GET /v1/models returned invalid JSON: ") + e.what());
        std::cout << "Endpoint returned invalid JSON.\n";
        return std::nullopt;
    }

    result.erase(std::unique(result.begin(), result.end()), result.end());
    if (result.empty()) {
        log_models_error("GET /v1/models returned an empty list");
        std::cout << "Endpoint returned no models.\n";
        return std::nullopt;
    }
    return result;
}

// ───────── 选择菜单：全屏方向键菜单，每帧从原点重绘，按终端宽窄自适应 ─────────

inline std::string ellipsize(const std::string& text, size_t max_cols) {
    if (text.size() <= max_cols) return text;
    if (max_cols <= 3) return text.substr(0, max_cols);
    size_t cut = max_cols - 3;
    while (cut > 0 && (static_cast<unsigned char>(text[cut]) & 0xC0) == 0x80) --cut;
    return text.substr(0, cut) + "...";
}

inline std::optional<std::string> select_model_menu(const std::vector<std::string>& items,
                                                    const std::string& current) {
    if (items.empty()) return std::nullopt;

    size_t selected = 0;
    std::optional<size_t> current_index;
    for (size_t i = 0; i < items.size(); ++i) {
        if (items[i] == current) {
            selected = i;
            current_index = i;
            break;
        }
    }

    constexpr size_t kPrefixCols = 2;
    constexpr size_t kSuffixCols = 11;

    line::RawMode raw;
    line::AlternateScreen screen;

    size_t top = 0;
    std::optional<std::string> result;
    bool done = false;

    while (!done) {
        const int rows = line::terminal_rows();
        const int cols = line::terminal_columns();

        const size_t fixed = kPrefixCols + kSuffixCols;
        const bool show_suffix = cols > static_cast<int>(fixed) + 2;
        const size_t fixed_cols = show_suffix ? fixed : kPrefixCols;
        const size_t budget = cols > static_cast<int>(fixed_cols) + 1
                                  ? static_cast<size_t>(cols) - fixed_cols - 1
                                  : size_t{1};

        size_t window = static_cast<size_t>(rows > 4 ? rows - 3 : 1);
        if (window > items.size()) window = items.size();
        if (window < 1) window = 1;

        if (selected < top) top = selected;
        else if (selected >= top + window) top = selected - window + 1;
        if (top + window > items.size()) top = items.size() - window;

        line::AlternateScreen::clear_frame();
        const std::string title =
            cols >= 62 ? "Select a model (\xE2\x86\x91/\xE2\x86\x93 move, Enter confirm, q/Esc cancel)"
            : cols >= 42 ? "Select a model (\xE2\x86\x91/\xE2\x86\x93, Enter, q/Esc)"
                         : "Select model";
        std::cout << ellipsize(title, static_cast<size_t>(cols > 1 ? cols - 1 : 1)) << "\n";
        for (size_t i = top; i < top + window; ++i) {
            std::string name = ellipsize(items[i], budget);
            if (i == selected) std::cout << "\033[7m> " << name << "\033[0m";
            else               std::cout << "  " << name;
            if (current_index && *current_index == i && show_suffix)
                std::cout << "  \033[90m(current)\033[0m";
            std::cout << "\n";
        }
        if (items.size() > window && cols >= 10) {
            std::cout << "\033[90m" << (top + 1) << "-" << (top + window) << "/"
                      << items.size() << "\033[0m";
        }
        std::cout << std::flush;

        line::KeyEvent ev = line::read_key();
        switch (ev.key) {
            case line::Key::Up:
                selected = (selected == 0) ? items.size() - 1 : selected - 1;
                break;
            case line::Key::Down:
                selected = (selected + 1) % items.size();
                break;
            case line::Key::Home:
                selected = 0;
                break;
            case line::Key::End:
                selected = items.size() - 1;
                break;
            case line::Key::Enter:
                result = items[selected];
                done = true;
                break;
            case line::Key::Escape:
            case line::Key::Interrupt:
                done = true;
                break;
            case line::Key::Character:
                if (ev.ch == 'q' || ev.ch == 'Q') done = true;
                break;
            default:
                break;
        }
    }
    return result;
}

// ───────── 对外入口：启动时确保选到模型，/models 命令重新拉取并选择 ─────────

inline bool ensure_model_selected() {
    if (models.empty()) {
        std::cout << "Fetching available models from " << clean_base_url() << " ...\n" << std::flush;
        if (auto fetched = fetch_models()) {
            models = *fetched;
            save_config();
        }
    }

    const bool valid = !current_model.empty() &&
                       (models.empty() ||
                        std::find(models.begin(), models.end(), current_model) != models.end());

    if (!valid) {
        if (models.size() == 1) {
            set_current_model(models.front());
            std::cout << "Model: " << current_model << "\n";
            return true;
        }
        if (!models.empty()) {
            auto picked = select_model_menu(models, current_model);
            if (picked) {
                set_current_model(*picked);
                std::cout << "Model set to: " << current_model << " (saved to config)\n";
                return true;
            }
        }
        if (current_model.empty()) {
            std::cout << "model not set. Enter model name: " << std::flush;
            std::string name;
            std::getline(std::cin, name);
            if (!name.empty()) {
                set_current_model(name);
                return true;
            }
        }
    }
    return !current_model.empty();
}

inline void handle_models_command() {
    std::cout << "Fetching models from " << clean_base_url() << " ...\n" << std::flush;

    if (auto fetched = fetch_models()) {
        models = *fetched;
        save_config();
        std::cout << "Found " << models.size() << " model(s).\n";
    } else if (!models.empty()) {
        std::cout << "Fetch failed, showing the cached list.\n";
    } else {
        std::cout << "Fetch failed and no cached model list; check base_url / api_key.\n";
        return;
    }

    auto picked = select_model_menu(models, current_model);
    if (!picked) {
        std::cout << "Cancelled. Current model: "
                  << (current_model.empty() ? "(none)" : current_model) << "\n";
        return;
    }
    set_current_model(*picked);
    std::cout << "Model set to: " << current_model << " (saved to config)\n";
}
