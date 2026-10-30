#include <iostream>
#include <string_view>
#include "../include/json/json.hpp"
#include <string>
#include "ui.h"
#include "log.h"
#include "config.h"
#include "chat.h"
#include "input.h"
#include "md.h"

using json = nlohmann::json;

// ───────── 入口：初始化日志与配置，然后进入对话主循环 ─────────
int main() {
    // ───────── 启动：初始化终端与日志，读取配置并检查 api_key ─────────
    line::enable_virtual_terminal();
    init_logger();
    init_config();
    if (api_key.empty()) {
        const std::string msg = "No valid api key. Set API_KEY (or OPENAI_API_KEY), or put api_key in config file.";
        auto logger = spdlog::get("openai");
        if (logger) logger->error("{}", msg);
        else spdlog::error("{}", msg);
        std::cerr << msg << std::endl;
        return 1;
    }

    std::string current_answer;
    std::string current_reasoning;
    std::string full_Question;

    show_logo();

    // ───────── 选模型：配置里没有列表时从端点拉取，并让用户选择 ─────────
    if (!ensure_model_selected()) {
        const std::string msg = "No model selected. Set \"models\" in the config file or run /models.";
        auto logger = spdlog::get("openai");
        if (logger) logger->error("{}", msg);
        else spdlog::error("{}", msg);
        std::cerr << msg << std::endl;
        return 1;
    }
    std::cout << "Endpoint: " << clean_base_url() << " | Model: " << current_model << "\n\n";

    json history = json::array();
    bool thinking_enabled = false;

    // ───────── 主循环：读一行 → 命令 / 累积多行 / 空行提交 ─────────
    while (true) {
        auto input = line::read_line(">>> ");
        if (!input) break;
        std::string Question = *input;
        if (!Question.empty()) line::add_history(Question);

        const CommandOutcome cmd = process_command(Question, thinking_enabled);
        if (cmd == CommandOutcome::Quit) break;
        if (cmd == CommandOutcome::Handled) continue;

        if (!Question.empty()) {
            full_Question += "\n" + Question;
            continue;
        }

        if (full_Question.empty()) continue;

        // 历史只追加：已发出去的前缀永远不改，服务端才能按前缀命中缓存
        history.push_back({{"role", "user"}, {"content", full_Question}});
        current_answer.clear();
        current_reasoning.clear();
        // ───────── 流式回复：思考内容走灰色前缀，正文交给 Markdown 渲染器 ─────────
        start_spin();
        bool first = true;
        bool thinking_label = true;

        md::StreamRenderer r;
        r.set_output(&std::cout);

        get_answer(history, thinking_enabled, [&](std::string_view line) {
            auto [thinking, content] = parse_one_line(line);

            if (!thinking.empty()) {
                current_reasoning += thinking;
                if (first) { stop_spin(); first = false; }
                if (thinking_label) {
                    std::cout << "\033[90m[Thinking] \n" << thinking <<std::flush;
                    thinking_label = false;
                } else {
                    std::cout << thinking<<std::flush;
                }
            }

            if (!content.empty()) {
                if (first) { stop_spin(); first = false; }
                if (thinking_label) {
                    thinking_label = false;
                } else {
                    std::cout << "\033[0m";
                }
                r.feed_chunk(std::string(content));
                std::cout << std::flush;
                current_answer += content;
            }
        });

        stop_spin();
        r.finish();

        // 原样写回本轮生成的内容（含思考），下一轮前缀与 KV 缓存中的 token 完全对齐；
        // 请求失败（什么都没收到）时不写空 assistant，避免污染历史前缀
        if (!current_answer.empty() || !current_reasoning.empty()) {
            json assistant = {{"role", "assistant"}, {"content", current_answer}};
            if (!current_reasoning.empty()) assistant["reasoning_content"] = current_reasoning;
            history.push_back(std::move(assistant));
        }
        full_Question.clear();
        current_answer.clear();
        current_reasoning.clear();
        std::cout << "\n";
    }
    return 0;
}
