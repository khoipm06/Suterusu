#ifndef BROWSER_SESSION_H
#define BROWSER_SESSION_H

#include "cdp_connection.h"

#include <memory>
#include <mutex>
#include <set>
#include <string>

struct BrowserAIConfig {
    std::string name;          // Session identifier (e.g., "chatgpt", "claude")
    std::string port;          // Debug port (e.g., "8265")
    std::string url_pattern;   // Regex pattern to match tab URL
    std::string script_path;   // Path to JS file to inject
    std::string function_name; // JS function to call
    std::string browser_path;  // Optional: browser executable path
    std::string user_data_dir; // Optional: user data directory for profile
    std::string start_url;     // Optional: URL to open on launch
};

class BrowserAISession {
  private:
    BrowserAIConfig config;
    std::unique_ptr<CDPConnection> connection;
    std::mutex session_mutex;

  public:
    BrowserAISession(const BrowserAIConfig &cfg);

    const BrowserAIConfig &GetConfig() const;

    bool Launch(std::set<std::string> &launched_ports);
    bool Connect();
    bool Reconnect();
    bool InjectScript();
    bool SendPrompt(const std::string &prompt, std::string &result,
                    const std::string &extract_tag = "answer");
    void Shutdown();
};

#endif // BROWSER_SESSION_H