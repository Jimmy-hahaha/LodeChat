#pragma once
#include <iostream>
#include <string>
#include "lineedit.h"
#include "models.h"

// ───────── 命令：把输入行认成内置命令并就地执行，其余交回主循环 ─────────

// ───────── 处理结果：NotCommand 当正文累积；Handled 已被消化（不能再进待发送缓冲区）；Quit 退出 ─────────
enum class CommandOutcome { NotCommand, Handled, Quit };

// ───────── 分发：命中内置命令就执行并回报状态，未命中原样送回主循环 ─────────
inline CommandOutcome process_command(const std::string& input, bool& thinking_enabled) {
    if (input == "/exit") {
        return CommandOutcome::Quit;
    }
    if (input == "/help") {
        std::cout << "Commands:\n";
        for (const auto& [name, description] : line::commands()) {
            std::cout << "  " << name << " - " << description << "\n";
        }
        return CommandOutcome::Handled;
    }
    if (input == "/models") {
        handle_models_command();
        return CommandOutcome::Handled;
    }
    if (input == "/model") {
        std::cout << "Endpoint: " << clean_base_url() << "\n";
        std::cout << "Model: " << (current_model.empty() ? "(none)" : current_model) << "\n";
        return CommandOutcome::Handled;
    }
    if (input == "/think") {
        thinking_enabled = true;
        return CommandOutcome::Handled;
    }
    if (input == "/think_off") {
        thinking_enabled = false;
        return CommandOutcome::Handled;
    }
    return CommandOutcome::NotCommand;
}
