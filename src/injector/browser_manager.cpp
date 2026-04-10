#include "browser_manager.h"
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

bool BrowserAIManager::LoadConfig(const std::string &config_path) {
    std::lock_guard<std::mutex> lock(manager_mutex);

    try {
        std::ifstream file(config_path);
        if (!file.is_open()) {
            std::cerr << "[BrowserAI] Cannot open config: " << config_path << "\n";
            return false;
        }

        auto jsonFile = json::parse(file);

        if (!jsonFile.contains("browser_ai")) {
            std::cout << "[BrowserAI] No browser_ai section in config\n";
            return true; // Not an error, just no sessions configured
        }

        // Shutdown existing sessions that are being reconfigured
        for (auto &[name, cfg] : jsonFile["browser_ai"].items()) {
            auto it = sessions.find(name);
            if (it != sessions.end()) {
                it->second->Shutdown();
                sessions.erase(it);
            }
        }

        // Load new configurations
        for (auto &[name, cfg] : jsonFile["browser_ai"].items()) {
            BrowserAIConfig config;
            config.name = name;

            if (cfg.contains("port"))
                config.port = cfg["port"].get<std::string>();
            if (cfg.contains("url_pattern"))
                config.url_pattern = cfg["url_pattern"].get<std::string>();
            if (cfg.contains("script"))
                config.script_path = cfg["script"].get<std::string>();
            if (cfg.contains("function"))
                config.function_name = cfg["function"].get<std::string>();
            if (cfg.contains("browser"))
                config.browser_path = cfg["browser"].get<std::string>();
            if (cfg.contains("profile"))
                config.user_data_dir = cfg["profile"].get<std::string>();
            if (cfg.contains("start_url"))
                config.start_url = cfg["start_url"].get<std::string>();

            sessions[name] = std::make_unique<BrowserAISession>(config);
            std::cout << "[BrowserAI] Loaded session: " << name << " (port " << config.port
                      << ", pattern: " << config.url_pattern << ")\n";
        }

        return true;

    } catch (std::exception const &e) {
        std::cerr << "[BrowserAI] Config error: " << e.what() << "\n";
        return false;
    }
}

BrowserAISession *BrowserAIManager::GetSession(const std::string &name) {
    std::lock_guard<std::mutex> lock(manager_mutex);

    auto it = sessions.find(name);
    if (it != sessions.end()) {
        return it->second.get();
    }

    std::cerr << "[BrowserAI] Session not found: " << name << "\n";
    return nullptr;
}

std::vector<std::string> BrowserAIManager::GetSessionNames() {
    std::lock_guard<std::mutex> lock(manager_mutex);

    std::vector<std::string> names;
    for (const auto &[name, _] : sessions) {
        names.push_back(name);
    }
    return names;
}

bool BrowserAIManager::LaunchAll() {
    std::vector<std::string> session_names;
    {
        std::lock_guard<std::mutex> lock(manager_mutex);
        for (const auto &[name, _] : sessions) {
            session_names.push_back(name);
        }
    }

    int success_count = 0;
    int failed_count = 0;

    for (const auto &session_name : session_names) {
        if (this->LaunchSession(session_name)) {
            success_count++;
        } else {
            failed_count++;
        }
    }

    std::cout << "[BrowserAI] LaunchAll complete. Success: " << success_count 
              << ", Failed: " << failed_count << "\n";

    return true;
}

bool BrowserAIManager::LaunchSession(const std::string &name) {
    std::lock_guard<std::mutex> lock(manager_mutex);

    auto it = sessions.find(name);
    if (it != sessions.end()) {
        if (!it->second->Launch(launched_ports)) {
            std::cerr << "[BrowserAI] Failed to launch session: " << name << "\n";
            return false;
        }
        std::cout << "[BrowserAI] Session launched: " << name << "\n";
        if (!it->second->Connect()) {
            std::cerr << "[BrowserAI] Failed to connect session: " << name << "\n";
            return false;
        }
        std::cout << "[BrowserAI] Session connected: " << name << "\n";
        return true;
    }
    std::cerr << "[BrowserAI] Session not found: " << name << "\n";
    return false;
}

void BrowserAIManager::ShutdownAll() {
    std::lock_guard<std::mutex> lock(manager_mutex);

    for (auto &[name, session] : sessions) {
        session->Shutdown();
    }
    sessions.clear();
}