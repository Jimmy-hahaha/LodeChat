// ───────── config_tool：问答式配置工具，不联网、不拉取模型 ─────────

#include <iostream>
#include <string>

#include "log.h"
#include "config.h"

namespace {

std::string ask(const std::string& label, const std::string& current) {
    std::cout << label;
    if (!current.empty()) std::cout << " [" << current << "]";
    std::cout << ": " << std::flush;
    std::string value;
    std::getline(std::cin, value);
    return value;
}

}

int main() {
    init_logger();

    fs::path file = cfg::locateConfigFile("llm-chat");
    if (file.empty()) {
        std::cerr << "unable to determine config path" << std::endl;
        return 1;
    }

    json config = cfg::defaultConfig();
    if (fs::exists(file)) {
        try {
            json on_disk = cfg::loadJson(file);
            if (on_disk.is_object()) config = on_disk;
        } catch (const std::exception& e) {
            std::cerr << "cannot read " << file.string() << ": " << e.what() << std::endl;
            std::cerr << "starting from defaults" << std::endl;
        }
    }

    json model_list = json::array();
    if (config.contains("models")) {
        const json& m = config["models"];
        if (m.is_array()) {
            model_list = m;
        } else if (m.is_string() && !m.get<std::string>().empty()) {
            model_list.push_back(m.get<std::string>());
        }
    }
    config["models"] = model_list;

    auto as_str = [&](const char* key) {
        return (config.contains(key) && config[key].is_string())
                   ? config[key].get<std::string>()
                   : std::string();
    };

    std::cout << "Config file: " << file.string() << "\n"
              << "Press Enter to keep the current value.\n\n";

    std::string value = ask("base_url", as_str("base_url"));
    if (!value.empty()) config["base_url"] = value;

    std::cout << "api_key (input is hidden)";
    if (!as_str("api_key").empty()) std::cout << " [keep current]";
    std::cout << ": " << std::flush;
    std::string key = read_masked();
    if (!key.empty()) config["api_key"] = key;

    value = ask("current_model", as_str("current_model"));
    if (!value.empty()) {
        config["current_model"] = value;
        bool known = false;
        for (const auto& m : config["models"]) {
            if (m.is_string() && m.get<std::string>() == value) known = true;
        }
        if (!known) config["models"].push_back(value);
    }

    if (config["models"].is_array() && !config["models"].empty()) {
        std::cout << "\nmodels:";
        for (const auto& m : config["models"]) {
            if (m.is_string()) std::cout << " " << m.get<std::string>();
        }
        std::cout << "\n";
    }

    try {
        cfg::saveJson(file, config);
        std::cout << "\nConfiguration saved to " << file.string() << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "failed to save config: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
