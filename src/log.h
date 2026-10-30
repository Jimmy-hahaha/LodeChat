#pragma once
#include <cstdlib>
#include <string>
#include <vector>
#include <memory>
#include "spdlog/spdlog.h"
#include "../include/spdlog/sinks/basic_file_sink.h"
#include "../include/spdlog/sinks/stdout_color_sinks.h"
#include "config.h"

// ───────── 日志：写可执行文件同目录的 openai.log 与终端 ─────────
// 默认落在 LodeChat / config_tool 旁边（即编译产物所在目录），不再跟随启动时的工作目录。
// LODE_LOG_FILE 可覆盖日志路径；LODE_LOG_CONSOLE=0（或 off）可关终端输出。
inline fs::path logFilePath() {
    if (const char* env = std::getenv("LODE_LOG_FILE"); env && *env) {
        return fs::path(env);
    }
    fs::path exeDir = cfg::getExecutablePath().parent_path();
    if (!exeDir.empty()) return exeDir / "openai.log";
    return fs::path("openai.log");
}

inline void init_logger() {
    std::vector<spdlog::sink_ptr> sinks;

    // 优先写可执行文件目录；该目录不可写时退回当前工作目录，再不行就只写终端
    fs::path file = logFilePath();
    try {
        sinks.push_back(std::make_shared<spdlog::sinks::basic_file_sink_mt>(file.string()));
    } catch (const spdlog::spdlog_ex&) {
        try {
            sinks.push_back(std::make_shared<spdlog::sinks::basic_file_sink_mt>("openai.log"));
        } catch (const spdlog::spdlog_ex&) {
        }
    }

    const char* console_env = std::getenv("LODE_LOG_CONSOLE");
    bool console = !(console_env && (std::string(console_env) == "0" ||
                                     std::string(console_env) == "off"));
    if (console) {
        sinks.push_back(std::make_shared<spdlog::sinks::stderr_color_sink_mt>());
    }

    auto logger = std::make_shared<spdlog::logger>("openai", sinks.begin(), sinks.end());
    logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] %v");
    logger->flush_on(spdlog::level::warn);
    spdlog::register_logger(logger);
}
