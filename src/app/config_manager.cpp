#include "config_manager.h"
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <sstream>

using json = nlohmann::json;

std::string API_URL;
std::string API_KEY;
std::string MODEL;
std::vector<std::string> FALLBACK_MODELS;
std::vector<std::string> AI_PROVIDERS;
std::string SYSTEM_PROMPT;
std::string FLASH_WINDOW = "Chrome";

bool LoadConfig() {
    std::ifstream promptFile("system_prompt.md");
    if (promptFile.is_open()) {
        std::stringstream buffer;
        buffer << promptFile.rdbuf();
        SYSTEM_PROMPT = buffer.str();
    } else {
        SYSTEM_PROMPT = "You are a helpful assistant.";
    }
    std::ifstream configFile("config.json");
    if (!configFile.is_open()) {
        std::cout << "Creating default config.json..." << std::endl;
        json defaultConfig = {{"api_url", "http://localhost:8080/v1/chat/completions"},
                              {"api_key", ""},
                              {"ai",
                               {{"model", "openai/gpt-3.5-turbo"},
                                {"fallback_models", json::array()},
                                {"providers", json::array()}}},
                              {"flash_window", "Chrome"}};
        std::ofstream outFile("config.json");
        if (outFile.is_open())
            outFile << defaultConfig.dump(2) << std::endl;
        API_URL = defaultConfig["api_url"];
        API_KEY = defaultConfig["api_key"];
        MODEL = defaultConfig["ai"]["model"];
        FLASH_WINDOW = defaultConfig["flash_window"];
        return true;
    }
    try {
        json config;
        configFile >> config;
        API_URL = config.value("api_url", "");
        API_KEY = config.value("api_key", "");
        FLASH_WINDOW = config.value("flash_window", "Chrome");

        // Parse ai section
        if (!config.contains("ai")) {
            std::cerr << "Error: config.json missing 'ai' section" << std::endl;
            return false;
        }

        json aiConfig = config["ai"];
        if (!aiConfig.contains("model") || aiConfig["model"].get<std::string>().empty()) {
            std::cerr << "Error: config.json missing 'ai.model'" << std::endl;
            return false;
        }
        MODEL = aiConfig["model"];

        // Parse fallback_models array (optional)
        if (aiConfig.contains("fallback_models") && aiConfig["fallback_models"].is_array()) {
            for (const auto &fallbackModel : aiConfig["fallback_models"]) {
                if (fallbackModel.is_string())
                    FALLBACK_MODELS.push_back(fallbackModel.get<std::string>());
            }
            if (!FALLBACK_MODELS.empty())
                std::cout << "[DEBUG] Loaded " << FALLBACK_MODELS.size() << " fallback model(s)"
                          << std::endl;
        }

        // Parse providers array (optional)
        if (aiConfig.contains("providers") && aiConfig["providers"].is_array()) {
            for (const auto &provider : aiConfig["providers"]) {
                if (provider.is_string())
                    AI_PROVIDERS.push_back(provider.get<std::string>());
            }
            if (!AI_PROVIDERS.empty())
                std::cout << "[DEBUG] Loaded " << AI_PROVIDERS.size()
                          << " provider routing preference(s)" << std::endl;
        }

        if (API_URL.empty()) {
            std::cerr << "Error: config.json missing 'api_url'" << std::endl;
            return false;
        }
        return true;
    } catch (const std::exception &e) {
        std::cerr << "Config error: " << e.what() << std::endl;
        return false;
    }
}