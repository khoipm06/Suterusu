#include <boost/asio.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/beast/version.hpp>
#include <nlohmann/json.hpp>

#include <windows.h>
#include <fstream>
#include <iostream>
#include <string>
#include <memory>
#include <mutex>
#include <atomic>
#include <map>
#include <set>
#include <vector>
#include <regex>

using tcp = boost::asio::ip::tcp;
namespace http = boost::beast::http;
namespace websocket = boost::beast::websocket;
using json = nlohmann::json;

// Target information for multi-tab support
struct TargetInfo {
    std::string id;
    std::string title;
    std::string url;
    std::string type;
    std::string webSocketDebuggerUrl;
};

// Persistent WebSocket connection manager with regex-based target filtering
class CDPConnection {
private:
    std::unique_ptr<boost::asio::io_context> ioc;
    std::unique_ptr<websocket::stream<tcp::socket>> ws;
    std::mutex ws_mutex;
    std::atomic<bool> connected{false};
    std::atomic<int> message_id{1};
    std::string ws_url;
    std::string debug_port;
    std::string url_pattern;  // Regex pattern for target matching
    std::string connected_target_id;
    
    // Read file content helper
    static std::string ReadFileContent(const std::string& script_path, const std::string& debug_port) {
        std::ifstream file(script_path);
        if (!file.is_open()) {
            std::cerr << "[CDP:" << debug_port << "] Cannot open: " << script_path << "\n";
            return "";
        }
        return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    }
    
    // Query /json endpoint to get available targets
    std::vector<TargetInfo> QueryTargets() {
        std::vector<TargetInfo> targets;
        
        try {
            const char* host = "127.0.0.1";
            const char* port = debug_port.c_str();
            
            if (!ioc) {
                ioc = std::make_unique<boost::asio::io_context>();
            }
            
            tcp::resolver resolver{*ioc};
            auto const results = resolver.resolve(host, port);
            
            boost::beast::tcp_stream http_stream{*ioc};
            http_stream.connect(results);
            
            http::request<http::string_body> req{http::verb::get, "/json", 11};
            req.set(http::field::host, host);
            req.set(http::field::user_agent, "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36");
            http::write(http_stream, req);
            
            boost::beast::flat_buffer buffer;
            http::response<http::string_body> res;
            http::read(http_stream, buffer, res);
            http_stream.socket().shutdown(tcp::socket::shutdown_both);
            
            auto j = json::parse(res.body());
            if (!j.is_array()) return targets;
            
            for (auto& entry : j) {
                TargetInfo info;
                if (entry.contains("id")) info.id = entry["id"].get<std::string>();
                if (entry.contains("title")) info.title = entry["title"].get<std::string>();
                if (entry.contains("url")) info.url = entry["url"].get<std::string>();
                if (entry.contains("type")) info.type = entry["type"].get<std::string>();
                if (entry.contains("webSocketDebuggerUrl")) 
                    info.webSocketDebuggerUrl = entry["webSocketDebuggerUrl"].get<std::string>();
                
                if (info.type == "page" && !info.webSocketDebuggerUrl.empty()) {
                    targets.push_back(info);
                }
            }
            
        } catch (std::exception const& e) {
            std::cerr << "[CDP:" << debug_port << "] QueryTargets error: " << e.what() << "\n";
        }
        
        return targets;
    }
    
    // Find target matching URL pattern (regex)
    TargetInfo FindMatchingTarget(const std::vector<TargetInfo>& targets) {
        if (url_pattern.empty() && !targets.empty()) {
            return targets[0];  // Return first target if no filter
        }
        
        try {
            std::regex pattern(url_pattern, std::regex::icase);
            for (const auto& target : targets) {
                if (std::regex_search(target.url, pattern)) {
                    return target;
                }
            }
        } catch (std::regex_error& e) {
            std::cerr << "[CDP:" << debug_port << "] Invalid regex pattern: " << e.what() << "\n";
        }
        
        return {};  // Empty target
    }
    
    bool ConnectToWebSocket(const std::string& ws_url_full) {
        try {
            // Here we assume che*ting on local machine, what is the purpose of remoting to other host, right?
            const char* host = "127.0.0.1";
            
            std::string ws_host = host;
            std::string ws_port = debug_port;
            std::string ws_path;
            
            {
                auto pos = ws_url_full.find("://");
                auto rest = (pos == std::string::npos) ? ws_url_full : ws_url_full.substr(pos + 3);
                auto slash_pos = rest.find('/');
                auto hostport = rest.substr(0, slash_pos);
                ws_path = rest.substr(slash_pos);
                
                auto colon_pos = hostport.find(':');
                if (colon_pos != std::string::npos) {
                    ws_host = hostport.substr(0, colon_pos);
                    ws_port = hostport.substr(colon_pos + 1);
                }
            }
            
            tcp::resolver ws_resolver{*ioc};
            auto const ws_results = ws_resolver.resolve(ws_host, ws_port);
            
            ws = std::make_unique<websocket::stream<tcp::socket>>(*ioc);
            boost::asio::connect(ws->next_layer(), ws_results);
            
            std::string host_header = ws_host + ":" + ws_port;
            ws->handshake(host_header, ws_path);
            
            connected = true;
            ws_url = ws_url_full;
            
            // Enable Page domain
            try {
                json enable_msg = {
                    {"id", message_id++},
                    {"method", "Page.enable"},
                    {"params", json::object()}
                };
                ws->write(boost::asio::buffer(enable_msg.dump()));
                boost::beast::flat_buffer enable_buffer;
                ws->read(enable_buffer);
            } catch (...) {}
            
            return true;
            
        } catch (std::exception const& e) {
            std::cerr << "[CDP:" << debug_port << "] WebSocket error: " << e.what() << "\n";
            connected = false;
            return false;
        }
    }
    
    bool EstablishConnection() {
        ioc = std::make_unique<boost::asio::io_context>();
        
        auto targets = QueryTargets();
        if (targets.empty()) {
            std::cerr << "[CDP:" << debug_port << "] No targets found\n";
            return false;
        }
        
        auto target = FindMatchingTarget(targets);
        if (target.webSocketDebuggerUrl.empty()) {
            std::cerr << "[CDP:" << debug_port << "] No matching target for pattern: " << url_pattern << "\n";
            std::cerr << "[CDP:" << debug_port << "] Available targets:\n";
            for (const auto& t : targets) {
                std::cerr << "  - " << t.title << " | " << t.url << "\n";
            }
            return false;
        }
        
        std::cout << "[CDP:" << debug_port << "] Connecting to: " << target.title << "\n";
        connected_target_id = target.id;
        return ConnectToWebSocket(target.webSocketDebuggerUrl);
    }
    
public:
    CDPConnection(const std::string& port, const std::string& pattern = "") 
        : debug_port(port), url_pattern(pattern) {}
    
    void SetUrlPattern(const std::string& pattern) { url_pattern = pattern; }
    std::string GetPort() const { return debug_port; }
    
    bool EnsureConnected() {
        std::lock_guard<std::mutex> lock(ws_mutex);
        
        if (connected && ws && ws->next_layer().is_open()) {
            return true;
        }
        
        std::cout << "[CDP:" << debug_port << "] Establishing connection...\n";
        return EstablishConnection();
    }
    
    bool SendCommand(const std::string& method, const json& params, std::string& response) {
        std::lock_guard<std::mutex> lock(ws_mutex);
        
        if (!connected || !ws) {
            std::cerr << "[CDP:" << debug_port << "] Not connected\n";
            return false;
        }
        
        try {
            // Verify socket is still open before sending
            if (!ws->next_layer().is_open()) {
                std::cerr << "[CDP] Socket is closed\n";
                connected = false;
                return false;
            }
            
            json msg = {
                {"id", message_id++},
                {"method", method},
                {"params", params}
            };
            
            ws->write(boost::asio::buffer(msg.dump()));
            
            boost::beast::flat_buffer ws_buffer;
            ws->read(ws_buffer);
            response = boost::beast::buffers_to_string(ws_buffer.data());
            
            return true;
            
        } catch (std::exception const& e) {
            std::cerr << "[CDP:" << debug_port << "] Send error: " << e.what() << "\n";
            connected = false;
            // Try to close the socket gracefully
            try {
                if (ws && ws->next_layer().is_open()) {
                    ws->next_layer().close();
                }
            } catch (...) {}
            return false;
        }
    }
    
    // Inject script for one-time execution (Runtime.evaluate)
    bool InjectScript(const std::string& script_path) {
        if (!EnsureConnected()) return false;
        
        try {
            std::string jsCode = ReadFileContent(script_path, debug_port);
            if (jsCode.empty()) return false;
            
            json eval_params = {
                {"expression", jsCode},
                {"userGesture", true},
                {"awaitPromise", false}
            };
            
            std::string response;
            if (!SendCommand("Runtime.evaluate", eval_params, response)) {
                return false;
            }
            
            std::cout << "[CDP:" << debug_port << "] Script injected: " << script_path << "\n";
            return true;
            
        } catch (std::exception const& e) {
            std::cerr << "[CDP:" << debug_port << "] InjectScript error: " << e.what() << "\n";
            return false;
        }
    }
    
    // Inject script persistently (runs on every page load)
    bool InjectPersistentScript(const std::string& script_path) {
        if (!EnsureConnected()) return false;
        
        try {
            std::string jsCode = ReadFileContent(script_path, debug_port);
            if (jsCode.empty()) return false;
            
            // Register for future page loads
            json params = {{"source", jsCode}};
            std::string response;
            if (!SendCommand("Page.addScriptToEvaluateOnNewDocument", params, response)) {
                return false;
            }
            
            // Also evaluate immediately
            json eval_params = {
                {"expression", jsCode},
                {"userGesture", true},
                {"awaitPromise", false}
            };
            SendCommand("Runtime.evaluate", eval_params, response);
            
            std::cout << "[CDP:" << debug_port << "] Persistent script: " << script_path << "\n";
            return true;
            
        } catch (std::exception const& e) {
            std::cerr << "[CDP:" << debug_port << "] InjectPersistentScript error: " << e.what() << "\n";
            return false;
        }
    }
    
    // Call a JavaScript function and get result
    bool SendPrompt(const std::string& function_name, const std::string& text, 
                    std::string& result, const std::string& extract_tag = "") {
        if (!EnsureConnected()) return false;
        
        try {
            std::string jsCall = "window." + function_name + "(" + json(text).dump() + ")";
            
            json eval_params = {
                {"expression", jsCall},
                {"userGesture", true},
                {"awaitPromise", true},
                {"returnByValue", true}
            };
            
            std::cout << "[CDP:" << debug_port << "] Calling " << function_name << "()...\n";
            
            std::string eval_response;
            if (!SendCommand("Runtime.evaluate", eval_params, eval_response)) {
                return false;
            }
            
            auto resp_json = json::parse(eval_response);
            
            if (resp_json.contains("result") && resp_json["result"].contains("result")) {
                auto res = resp_json["result"]["result"];
                
                if (res.contains("value")) {
                    std::string rawResult = res["value"].get<std::string>();
                    
                    // Extract content from tag if specified
                    if (!extract_tag.empty()) {
                        std::string openTag = "<" + extract_tag + ">";
                        std::string closeTag = "</" + extract_tag + ">";
                        
                        size_t start = rawResult.find(openTag);
                        size_t end = rawResult.find(closeTag);
                        
                        if (start != std::string::npos && end != std::string::npos) {
                            start += openTag.length();
                            result = rawResult.substr(start, end - start);
                            
                            // Trim whitespace
                            size_t first = result.find_first_not_of(" \t\n\r");
                            size_t last = result.find_last_not_of(" \t\n\r");
                            if (first != std::string::npos && last != std::string::npos) {
                                result = result.substr(first, last - first + 1);
                            }
                        } else {
                            result = rawResult;
                        }
                    } else {
                        result = rawResult;
                    }
                    
                    std::cout << "[CDP:" << debug_port << "] Got response (" << result.length() << " chars)\n";
                    return true;
                    
                } else if (res.contains("exceptionDetails")) {
                    std::cerr << "[CDP:" << debug_port << "] JS error: " 
                              << res["exceptionDetails"].dump() << "\n";
                    return false;
                }
            }
            
            std::cerr << "[CDP:" << debug_port << "] Unexpected response format\n";
            return false;
            
        } catch (std::exception const& e) {
            std::cerr << "[CDP:" << debug_port << "] SendPrompt error: " << e.what() << "\n";
            return false;
        }
    }
    
    void Disconnect() {
        std::lock_guard<std::mutex> lock(ws_mutex);
        
        if (ws && connected) {
            try { ws->close(websocket::close_code::normal); } catch (...) {}
        }
        
        ws.reset();
        connected = false;
        connected_target_id.clear();
        std::cout << "[CDP:" << debug_port << "] Disconnected\n";
    }
    
    bool ForceReconnect() {
        std::lock_guard<std::mutex> lock(ws_mutex);
        
        if (ws && connected) {
            try { ws->close(websocket::close_code::normal); } catch (...) {}
            ws.reset();
        }
        
        connected = false;
        ws_url.clear();
        connected_target_id.clear();
        
        return EstablishConnection();
    }
    
    ~CDPConnection() {
        Disconnect();
    }
};

// ============================================================================
// Browser AI Session Configuration
// ============================================================================

struct BrowserAIConfig {
    std::string name;           // Session identifier (e.g., "chatgpt", "claude")
    std::string port;           // Debug port (e.g., "8265")
    std::string url_pattern;    // Regex pattern to match tab URL
    std::string script_path;    // Path to JS file to inject
    std::string function_name;  // JS function to call
    std::string browser_path;   // Optional: browser executable path
    std::string user_data_dir;  // Optional: user data directory for profile
    std::string start_url;      // Optional: URL to open on launch
};

// ============================================================================
// Browser AI Session - Wraps CDPConnection with config
// ============================================================================

static std::string GetChromePath() {
    char buffer[MAX_PATH];
    DWORD bufferSize = sizeof(buffer);

    if (RegGetValueA(HKEY_LOCAL_MACHINE, "SOFTWARE\\Classes\\ChromeHTML\\shell\\open\\command",
                     nullptr, RRF_RT_REG_SZ, nullptr, buffer, &bufferSize) == ERROR_SUCCESS) {
        std::string path(buffer);
        // The registry key output looks like: "C:\Program Files\Google\Chrome\Application\chrome.exe" --single-argument %1
        size_t exe_pos = path.find(".exe");
        if (exe_pos != std::string::npos) {
            path = path.substr(0, exe_pos + 4);
            // Strip leading quote
            if (!path.empty() && path[0] == '\"') {
                path = path.substr(1);
            }
        }
        return path;
    }
    return "";
}

class BrowserAISession {
private:
    BrowserAIConfig config;
    std::unique_ptr<CDPConnection> connection;
    std::mutex session_mutex;
    
public:
    BrowserAISession(const BrowserAIConfig& cfg) : config(cfg) {}
    
    const BrowserAIConfig& GetConfig() const { return config; }
    
    bool Launch(std::set<std::string>& launched_ports) {
        if (launched_ports.count(config.port)) {
            std::cout << "[Session:" << config.name << "] Browser already launched for port " << config.port << "\n";
            return true;
        }

        if (!config.browser_path.empty()) {
            std::cout << "[Session:" << config.name << "] Using configured Chrome path: "
                    << config.browser_path << "\n";
        } else {
            std::cerr << "[Session:" << config.name << "] No browser path configured.\n";
            std::string detected = GetChromePath();
            if (!detected.empty()) {
                config.browser_path = detected;
                std::cout << "[Session:" << config.name << "] Using auto-detected Chrome path: "
                        << config.browser_path << "\n";
            } else {
                config.browser_path = "C:\\Program Files\\Google\\Chrome\\Application\\chrome.exe";
                std::cerr << "[Session:" << config.name << "] Failed to detect Chrome path.\n";
                std::cout << "[Session:" << config.name << "] Defaulting to: "
                        << config.browser_path << "\n";
            }
        }

        if (!std::filesystem::exists(config.browser_path)) {
            std::cerr << "[Session:" << config.name << "] Chrome path does not exist: "
                    << config.browser_path << "\n";
            return false;
        }

        std::string cmdLine = "\"" + config.browser_path + "\"";
        cmdLine += " --remote-debugging-port=" + config.port;
        
        if (!config.user_data_dir.empty()) {
            cmdLine += " --user-data-dir=\"" + config.user_data_dir + "\"";
        }
        
        if (!config.start_url.empty()) {
            cmdLine += " \"" + config.start_url + "\"";
        }
        
        std::cout << "[Session:" << config.name << "] Launching: " << cmdLine << "\n";
        
        STARTUPINFOA si;
        ZeroMemory(&si, sizeof(si));
        si.cb = sizeof(si);
        PROCESS_INFORMATION pi;
        ZeroMemory(&pi, sizeof(pi));
        
        if (!CreateProcessA(nullptr, const_cast<char*>(cmdLine.c_str()),
                           nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
            std::cerr << "[Session:" << config.name << "] Failed to launch. Error: " << GetLastError() << "\n";
            return false;
        }
        
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        
        launched_ports.insert(config.port);
        std::cout << "[Session:" << config.name << "] Browser launched\n";
        return true;
    }
    
    bool Connect() {
        std::lock_guard<std::mutex> lock(session_mutex);
        
        if (!connection) {
            connection = std::make_unique<CDPConnection>(config.port, config.url_pattern);
        }
        
        if (!connection->EnsureConnected()) {
            return false;
        }
        
        // Inject script if configured
        if (!config.script_path.empty()) {
            return connection->InjectScript(config.script_path);
        }
        
        return true;
    }
    
    bool Reconnect() {
        std::lock_guard<std::mutex> lock(session_mutex);
        
        if (!connection) {
            connection = std::make_unique<CDPConnection>(config.port, config.url_pattern);
        }
        
        if (!connection->ForceReconnect()) {
            return false;
        }
        
        // Re-inject script
        if (!config.script_path.empty()) {
            return connection->InjectScript(config.script_path);
        }
        
        return true;
    }
    
    bool InjectScript() {
        std::lock_guard<std::mutex> lock(session_mutex);
        
        if (!connection || config.script_path.empty()) {
            return false;
        }
        
        return connection->InjectScript(config.script_path);
    }
    
    bool SendPrompt(const std::string& prompt, std::string& result, const std::string& extract_tag = "answer") {
        std::lock_guard<std::mutex> lock(session_mutex);
        
        if (!connection) {
            connection = std::make_unique<CDPConnection>(config.port, config.url_pattern);
        }
        
        if (!connection->EnsureConnected()) {
            return false;
        }
        
        return connection->SendPrompt(config.function_name, prompt, result, extract_tag);
    }
    
    void Shutdown() {
        std::lock_guard<std::mutex> lock(session_mutex);
        
        if (connection) {
            connection->Disconnect();
            connection.reset();
        }
    }
};

// ============================================================================
// Browser AI Manager - Manages multiple sessions
// ============================================================================

class BrowserAIManager {
private:
    std::map<std::string, std::unique_ptr<BrowserAISession>> sessions;
    std::set<std::string> launched_ports;
    std::mutex manager_mutex;
    
public:
    bool LoadConfig(const std::string& config_path) {
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
                return true;  // Not an error, just no sessions configured
            }
            
            // Shutdown existing sessions that are being reconfigured
            for (auto& [name, cfg] : jsonFile["browser_ai"].items()) {
                auto it = sessions.find(name);
                if (it != sessions.end()) {
                    it->second->Shutdown();
                    sessions.erase(it);
                }
            }
            
            // Load new configurations
            for (auto& [name, cfg] : jsonFile["browser_ai"].items()) {
                BrowserAIConfig config;
                config.name = name;
                
                if (cfg.contains("port")) config.port = cfg["port"].get<std::string>();
                if (cfg.contains("url_pattern")) config.url_pattern = cfg["url_pattern"].get<std::string>();
                if (cfg.contains("script")) config.script_path = cfg["script"].get<std::string>();
                if (cfg.contains("function")) config.function_name = cfg["function"].get<std::string>();
                if (cfg.contains("browser")) config.browser_path = cfg["browser"].get<std::string>();
                if (cfg.contains("profile")) config.user_data_dir = cfg["profile"].get<std::string>();
                if (cfg.contains("start_url")) config.start_url = cfg["start_url"].get<std::string>();
                
                sessions[name] = std::make_unique<BrowserAISession>(config);
                std::cout << "[BrowserAI] Loaded session: " << name 
                          << " (port " << config.port << ", pattern: " << config.url_pattern << ")\n";
            }
            
            return true;
            
        } catch (std::exception const& e) {
            std::cerr << "[BrowserAI] Config error: " << e.what() << "\n";
            return false;
        }
    }
    
    BrowserAISession* GetSession(const std::string& name) {
        std::lock_guard<std::mutex> lock(manager_mutex);
        
        auto it = sessions.find(name);
        if (it != sessions.end()) {
            return it->second.get();
        }
        
        std::cerr << "[BrowserAI] Session not found: " << name << "\n";
        return nullptr;
    }
    
    std::vector<std::string> GetSessionNames() {
        std::lock_guard<std::mutex> lock(manager_mutex);
        
        std::vector<std::string> names;
        for (const auto& [name, _] : sessions) {
            names.push_back(name);
        }
        return names;
    }
    
    void LaunchAll() {
        std::lock_guard<std::mutex> lock(manager_mutex);
        
        for (auto& [name, session] : sessions) {
            session->Launch(launched_ports);
        }
    }
    
    bool LaunchSession(const std::string& name) {
        std::lock_guard<std::mutex> lock(manager_mutex);
        
        auto it = sessions.find(name);
        if (it != sessions.end()) {
            return it->second->Launch(launched_ports);
        }
        return false;
    }
    
    void ShutdownAll() {
        std::lock_guard<std::mutex> lock(manager_mutex);
        
        for (auto& [name, session] : sessions) {
            session->Shutdown();
        }
        sessions.clear();
    }
};

// ============================================================================
// Global instances
// ============================================================================

static std::unique_ptr<CDPConnection> g_main_cdp;  // Main browser (port 27245)
static std::mutex g_main_mutex;
static BrowserAIManager g_session_manager;

// ============================================================================
// Main browser exports
// ============================================================================

extern "C" __declspec(dllexport) bool InitializeCDP() {
    std::lock_guard<std::mutex> lock(g_main_mutex);
    if (!g_main_cdp) {
        g_main_cdp = std::make_unique<CDPConnection>("27245");
    }
    return g_main_cdp->EnsureConnected();
}

extern "C" __declspec(dllexport) void ShutdownCDP() {
    std::lock_guard<std::mutex> lock(g_main_mutex);
    if (g_main_cdp) {
        g_main_cdp->Disconnect();
        g_main_cdp.reset();
    }
}

extern "C" __declspec(dllexport) bool ReconnectCDP() {
    std::lock_guard<std::mutex> lock(g_main_mutex);
    if (!g_main_cdp) {
        g_main_cdp = std::make_unique<CDPConnection>("27245");
    }
    return g_main_cdp->ForceReconnect();
}

extern "C" __declspec(dllexport) bool InjectJavaScript(const char* filename) {
    std::lock_guard<std::mutex> lock(g_main_mutex);
    if (!g_main_cdp) {
        g_main_cdp = std::make_unique<CDPConnection>("27245");
    }
    return g_main_cdp->InjectPersistentScript(filename);
}

// ============================================================================
// Browser AI Session exports
// ============================================================================

extern "C" __declspec(dllexport) bool SessionStart(const char* config_path, const char* session_name = "") {
    g_session_manager.LoadConfig(config_path);
    if (!session_name || strlen(session_name) == 0) {
        g_session_manager.LaunchAll();
        return true;
    } else {
        return g_session_manager.LaunchSession(session_name);
    }
    return false;
}

extern "C" __declspec(dllexport) bool SessionReconnect(const char* name) {
    auto* session = g_session_manager.GetSession(name);
    return session ? session->Reconnect() : false;
}

extern "C" __declspec(dllexport) bool SessionInjectScript(const char* name) {
    auto* session = g_session_manager.GetSession(name);
    return session ? session->InjectScript() : false;
}

extern "C" __declspec(dllexport) bool SessionSend(const char* name, const char* prompt, 
                                                   char* result_buffer, int buffer_size, 
                                                   const char* extract_tag) {
    auto* session = g_session_manager.GetSession(name);
    if (!session) return false;
    
    std::string result;
    std::string tag = extract_tag ? extract_tag : "answer";
    
    if (!session->SendPrompt(prompt, result, tag)) {
        return false;
    }
    
    strncpy(result_buffer, result.c_str(), buffer_size - 1);
    result_buffer[buffer_size - 1] = '\0';
    return true;
}

extern "C" __declspec(dllexport) void SessionEnd(const char* name) {
    auto* session = g_session_manager.GetSession(name);
    if (session) session->Shutdown();
}

extern "C" __declspec(dllexport) void SessionsShutdownAll() {
    g_session_manager.ShutdownAll();
}

