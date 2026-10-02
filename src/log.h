#pragma once
#include <cstdlib>
#include <string>
#include <vector>
#include <memory>
#include "spdlog/spdlog.h"
#include "../include/spdlog/sinks/basic_file_sink.h"
#include "../include/spdlog/sinks/stdout_color_sinks.h"

// ───────── 日志：同时写 openai.log 与终端，LODE_LOG_CONSOLE=0 可关终端输出 ─────────
void init_logger() {
    std::vector<spdlog::sink_ptr> sinks;
    sinks.push_back(std::make_shared<spdlog::sinks::basic_file_sink_mt>("openai.log"));

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
