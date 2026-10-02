#pragma once
#define CPPHTTPLIB_OPENSSL_SUPPORT
#include "../include/cpp-httplib/httplib.h"
#include "../include/json/json.hpp"
#include <functional>
#include <utility>
#include "config.h"
#include "log.h"
using json = nlohmann::json;

// ───────── 对话：POST /v1/chat/completions，按 SSE 逐行取回复 ─────────

// ───────── 发起请求：stream=true，响应按行回调给上层，连接超时 50s / 读取超时 600s ─────────
inline void get_answer(const json& messages, bool enable_thinking, std::function<void(std::string_view)> chunk_handler){
    httplib::Client cli(base_url);
    cli.set_connection_timeout(50);
    cli.set_read_timeout(600);
    json body = {
    {"model", current_model},
    {"stream", true},
    {"temperature", 0.7},
    {"enable_thinking", enable_thinking},
    {"messages", messages}
    };
    std::string buffer;
    auto res = cli.Post(
        "/v1/chat/completions",
        {
            {"Authorization", std::string("Bearer ") + api_key},
            {"Content-Type", "application/json"}
        },
        body.dump(),
        "application/json",
        [&chunk_handler,&buffer](const char* data, size_t len) -> bool {
            buffer.append(data, len);
            size_t pos = 0;
            while (pos < buffer.size()) {
                size_t end = buffer.find('\n', pos);
                if (end == std::string::npos) break;
                std::string line = buffer.substr(pos, end - pos);
                pos = end + 1;
                if (!line.empty() && line.back() == '\r') line.pop_back();
                chunk_handler(line);
            }
            if (pos < buffer.size()) {
                buffer = buffer.substr(pos);
            } else {
                buffer.clear();
            }
            return true;
        }
    );
    if (!res) {
        spdlog::error("HTTP request failed: cannot connect to {}", base_url);
        return;
    }
    if (res->status != 200) {
        spdlog::error("API error: HTTP {} - {}", res->status, res->body);
        return;
    }
}

// ───────── 解析片段：从 data: 行里拆出 reasoning_content 与 content ─────────
inline auto parse_one_line(std::string_view line) -> std::pair<std::string, std::string> {
    if (line.find("data: ") == 0) {
        auto json_str = line.substr(6);
        if (json_str == "[DONE]") return {"", ""};
        try {
            json j = json::parse(json_str);
            if (j.contains("choices") && !j["choices"].empty()) {
                auto& choice = j["choices"][0];
                if (choice.contains("delta")) {
                    std::string thinking, content;
                    if (choice["delta"].contains("reasoning_content") && choice["delta"]["reasoning_content"].is_string()) {
                        thinking = choice["delta"]["reasoning_content"].get<std::string>();
                    }
                    if (choice["delta"].contains("content") && choice["delta"]["content"].is_string()) {
                        content = choice["delta"]["content"].get<std::string>();
                    }
                    return {thinking, content};
                }
            }
        } catch (...) { }
    }
    return {"", ""};
}
