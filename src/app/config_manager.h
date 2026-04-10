#ifndef CONFIG_MANAGER_H
#define CONFIG_MANAGER_H

#include <string>
#include <vector>

extern std::string API_URL;
extern std::string API_KEY;
extern std::string MODEL;
extern std::vector<std::string> FALLBACK_MODELS; // Array of fallback models for automatic failover
extern std::vector<std::string> AI_PROVIDERS;    // Array of provider names for OpenRouter routing
extern std::string SYSTEM_PROMPT;
extern std::string FLASH_WINDOW;

bool LoadConfig();

#endif // CONFIG_MANAGER_H