#pragma once

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include <algorithm>
#include <initializer_list>
#include <cstdlib>
#include <iostream>
#include "../include/json/json.hpp"
#include "spdlog/spdlog.h"

#if defined(_WIN32)
#include <windows.h>
#include <io.h>
#include <conio.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#include <climits>
#include <unistd.h>
#include <termios.h>
#elif defined(__linux__)
#include <unistd.h>
#include <limits.h>
#include <termios.h>
#endif

namespace fs = std::filesystem;
using json = nlohmann::json;

// ───────── 配置：定位 / 读写 config.json，环境变量优先，缺项交互式补问 ─────────
namespace cfg {

// ───────── 路径探测：定位可执行文件、家目录与平台约定的配置目录 ─────────
inline fs::path getExecutablePath() {
#if defined(_WIN32)
    wchar_t buf[MAX_PATH];
    DWORD len = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    if (len == 0 || len == MAX_PATH) return {};
    return fs::path(buf);
#elif defined(__APPLE__)
    char buf[PATH_MAX];
    uint32_t size = sizeof(buf);
    if (_NSGetExecutablePath(buf, &size) != 0) return {};
    return fs::weakly_canonical(fs::path(buf));
#elif defined(__linux__)
    char buf[PATH_MAX];
    ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (len <= 0) return {};
    buf[len] = '\0';
    return fs::path(buf);
#else
    return {};
#endif
}

inline fs::path getHomeDir() {
#if defined(_WIN32)
    const wchar_t* home = _wgetenv(L"USERPROFILE");
    if (home) return fs::path(home);
    const wchar_t* drive = _wgetenv(L"HOMEDRIVE");
    const wchar_t* path = _wgetenv(L"HOMEPATH");
    if (drive && path) return fs::path(std::wstring(drive) + path);
    return {};
#else
    const char* home = std::getenv("HOME");
    return home ? fs::path(home) : fs::path();
#endif
}

inline fs::path getUserConfigDir(const std::string& appName) {
#if defined(_WIN32)
    const wchar_t* appdata = _wgetenv(L"APPDATA");
    if (appdata) return fs::path(appdata) / appName;
    return {};
#elif defined(__APPLE__)
    return getHomeDir() / "Library" / "Application Support" / appName;
#elif defined(__linux__)
    const char* xdg = std::getenv("XDG_CONFIG_HOME");
    if (xdg && *xdg) return fs::path(xdg) / appName;
    return getHomeDir() / ".config" / appName;
#else
    return getHomeDir() / ("." + appName);
#endif
}

// ───────── 配置查找：MYAPP_CONFIG → 用户配置目录 → 可执行文件目录 → 当前工作目录 ─────────
inline fs::path locateConfigFile(const std::string& appName,
                                 const std::string& fileName = "config.json") {
    if (const char* env = std::getenv("MYAPP_CONFIG")) {
        if (*env) return fs::path(env);
    }

    fs::path userConfig = getUserConfigDir(appName) / fileName;
    if (!userConfig.empty() && fs::exists(userConfig)) return userConfig;

    fs::path exeDir = getExecutablePath().parent_path();
    if (!exeDir.empty()) {
        fs::path exeConfig = exeDir / fileName;
        if (fs::exists(exeConfig)) return exeConfig;
    }

    fs::path cwdConfig = fs::current_path() / fileName;
    if (fs::exists(cwdConfig)) return cwdConfig;

    return userConfig;
}

// ───────── JSON 读写：config.json 的载入、保存与默认模板 ─────────
inline json loadJson(const fs::path& file) {
    std::ifstream ifs(file);
    if (!ifs.is_open()) {
        throw std::runtime_error("unable to open config file: " + file.string());
    }
    return json::parse(ifs);
}

inline void saveJson(const fs::path& file, const json& j) {
    if (!file.parent_path().empty()) {
        fs::create_directories(file.parent_path());
    }
    std::ofstream ofs(file);
    if (!ofs.is_open()) {
        throw std::runtime_error("unable to write config file: " + file.string());
    }
    ofs << j.dump(4);
}

inline json defaultConfig() {
    return json{
        {"base_url", "http://0.0.0.0:8088"},
        {"models", json::array()},
        {"current_model", ""},
        {"api_key", ""},
        {"debug", true},
        {"log_level", "info"}
    };
}

}

// ───────── 载入与取值：读 config.json 并取出字符串 / 字符串数组，出错只记日志 ─────────
namespace {
void log_config_error(const std::string& msg) {
    auto logger = spdlog::get("openai");
    if (logger) {
        logger->error("{}", msg);
    } else {
        spdlog::error("{}", msg);
    }
}

json load_config() {
    fs::path file = cfg::locateConfigFile("llm-chat");
    if (file.empty()) {
        log_config_error("unable to determine config path");
        return {};
    }
    if (!fs::exists(file)) {
        try {
            cfg::saveJson(file, cfg::defaultConfig());
            log_config_error("config file created, set api_key in " + file.string());
        } catch (const std::exception& e) {
            log_config_error(e.what());
        }
        return {};
    }
    try {
        json j = cfg::loadJson(file);
        if (!j.is_object()) {
            log_config_error("config is not a JSON object: " + file.string());
            return {};
        }
        return j;
    } catch (const std::exception& e) {
        log_config_error(e.what());
        return {};
    }
}

std::string get_str(const json& j, const std::string& name) {
    if (j.is_object() && j.contains(name) && j[name].is_string()) {
        return j[name].get<std::string>();
    }
    return {};
}

std::vector<std::string> get_str_list(const json& j, const std::string& name) {
    std::vector<std::string> out;
    if (!j.is_object() || !j.contains(name)) return out;
    const json& v = j[name];
    if (v.is_array()) {
        for (const auto& item : v) {
            if (item.is_string()) out.push_back(item.get<std::string>());
        }
    } else if (v.is_string()) {
        std::string single = v.get<std::string>();
        if (!single.empty()) out.push_back(std::move(single));
    }
    return out;
}

// ───────── 环境变量：按优先级取第一个非空值（API_URL / API_KEY 优先） ─────────
std::string first_env(std::initializer_list<const char*> names) {
    for (const char* name : names) {
        if (const char* value = std::getenv(name); value && *value) return std::string(value);
    }
    return {};
}

// ───────── 隐藏输入：不回显地读一行，供 api_key 使用 ─────────
std::string read_masked() {
    std::string s;
#if defined(_WIN32)
    std::cout << std::flush;
    int ch;
    while ((ch = _getch()) != '\r' && ch != '\n') {
        if (ch == '\b' || ch == 0x7f) {
            if (!s.empty()) {
                s.pop_back();
                std::cout << "\b \b" << std::flush;
            }
        } else {
            s.push_back((char)ch);
            std::cout << '*' << std::flush;
        }
    }
    std::cout << '\n';
#else
    termios oldt, newt;
    if (tcgetattr(STDIN_FILENO, &oldt) != 0) {
        std::getline(std::cin, s);
        return s;
    }
    newt = oldt;
    newt.c_lflag &= ~(ECHO | ICANON);
    tcsetattr(STDIN_FILENO, TCSANOW, &newt);
    char c;
    while (std::cin.get(c)) {
        if (c == '\n' || c == '\r') break;
        if (c == '\b' || c == 127) {
            if (!s.empty()) {
                s.pop_back();
                std::cout << "\b \b" << std::flush;
            }
            continue;
        }
        s.push_back(c);
        std::cout << '*' << std::flush;
    }
    tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
    std::cout << '\n';
#endif
    return s;
}
}

// ───────── 全局配置项：base_url / models / current_model / api_key，客户端与 config_tool 共享 ─────────
inline std::string base_url = {};
inline std::vector<std::string> models = {};
inline std::string current_model = {};
inline std::string api_key = {};

inline fs::path config_path() {
    return cfg::locateConfigFile("llm-chat");
}

// ───────── 配置写回：只覆盖四个键，文件中已有的其他键（debug / log_level 等）保持不动 ─────────
inline void save_config() {
    fs::path file = config_path();
    if (file.empty()) {
        log_config_error("unable to determine config path");
        return;
    }
    json j = cfg::defaultConfig();
    if (fs::exists(file)) {
        try {
            json on_disk = cfg::loadJson(file);
            if (on_disk.is_object()) j = std::move(on_disk);
        } catch (const std::exception& e) {
            log_config_error(std::string("cannot read existing config, rewriting it: ") + e.what());
        }
    }
    j["base_url"] = base_url;
    j["models"] = models;
    j["current_model"] = current_model;
    j["api_key"] = api_key;
    try {
        cfg::saveJson(file, j);
    } catch (const std::exception& e) {
        log_config_error(e.what());
        std::cout << "Failed to save config: " << e.what() << std::endl;
    }
}

// ───────── 切换模型：补进模型列表并写入 config ─────────
inline bool set_current_model(const std::string& name) {
    if (name.empty()) return false;
    if (std::find(models.begin(), models.end(), name) == models.end()) {
        models.push_back(name);
    }
    current_model = name;
    save_config();
    return true;
}

// ───────── 初始化：环境变量优先，缺 base_url / api_key 时交互式询问并保存 ─────────
inline void init_config() {
    base_url = first_env({"OPENAI_BASE_URL", "OPENAI_API_URL", "API_URL"});
    api_key = first_env({"OPENAI_API_KEY", "API_KEY"});

    fs::path file = config_path();
    if (file.empty()) {
        log_config_error("unable to determine config path");
        return;
    }

    json j = load_config();
    if (base_url.empty()) base_url = get_str(j, "base_url");
    if (api_key.empty()) api_key = get_str(j, "api_key");
    models = get_str_list(j, "models");
    current_model = get_str(j, "current_model");

    if (current_model.empty() && models.size() == 1) {
        current_model = models.front();
    }

    bool changed = false;
    if (base_url.empty()) {
        std::cout << "base_url not set. Enter base_url: " << std::flush;
        std::getline(std::cin, base_url);
        changed = true;
    }
    if (api_key.empty()) {
        std::cout << "api_key not set. Enter api_key (input is hidden): " << std::flush;
        api_key = read_masked();
        changed = true;
    }

    if (changed) {
        save_config();
        std::cout << "Configuration saved to " << file.string() << std::endl;
    }
}
