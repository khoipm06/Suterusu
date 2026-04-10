#include "browser_session.h"
#include <boost/asio.hpp>
#include <filesystem>
#include <iostream>
#include <vector>
#include <windows.h>

using tcp = boost::asio::ip::tcp;

static std::string GetChromePath() {
    char buffer[MAX_PATH];
    DWORD bufferSize = sizeof(buffer);

    if (RegGetValueA(HKEY_LOCAL_MACHINE, "SOFTWARE\\Classes\\ChromeHTML\\shell\\open\\command",
                     nullptr, RRF_RT_REG_SZ, nullptr, buffer, &bufferSize) == ERROR_SUCCESS) {
        std::string path(buffer);
        // The registry key output looks like: "C:\Program
        // Files\Google\Chrome\Application\chrome.exe" --single-argument %1
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

static HANDLE g_hJob = NULL;

static void AssignProcessToJobForCleanup(HANDLE hProcess) {
    if (!g_hJob) {
        g_hJob = CreateJobObjectA(NULL, NULL);
        if (g_hJob) {
            JOBOBJECT_EXTENDED_LIMIT_INFORMATION jeli{};
            jeli.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
            SetInformationJobObject(g_hJob, JobObjectExtendedLimitInformation, &jeli, sizeof(jeli));
        }
    }
    if (g_hJob) {
        AssignProcessToJobObject(g_hJob, hProcess);
    }
}

static bool IsBrowserDebugPortOpen(const std::string& port) {
    try {
        boost::asio::io_context ioc;
        tcp::resolver resolver(ioc);
        auto results = resolver.resolve("127.0.0.1", port);
        tcp::socket socket(ioc);
        boost::asio::connect(socket, results);
        return true;
    } catch (...) {
        return false;
    }
}

BrowserAISession::BrowserAISession(const BrowserAIConfig &cfg) : config(cfg) {}

const BrowserAIConfig &BrowserAISession::GetConfig() const { return config; }

bool BrowserAISession::Launch(std::set<std::string> &launched_ports) {
    if (launched_ports.count(config.port)) {
        std::cout << "[Session:" << config.name << "] Browser already launched for port "
                  << config.port << "\n";
        return true;
    }

    if (IsBrowserDebugPortOpen(config.port)) {
        std::cout << "[Session:" << config.name << "] Browser already running on port " 
                  << config.port << ". Reusing instance.\n";
        launched_ports.insert(config.port);
        return true;
    }

    if (!config.browser_path.empty()) {
        std::cout << "[Session:" << config.name
                  << "] Using configured Chrome path: " << config.browser_path << "\n";
    } else {
        std::cerr << "[Session:" << config.name << "] No browser path configured.\n";
        std::string detected = GetChromePath();
        if (!detected.empty()) {
            config.browser_path = detected;
            std::cout << "[Session:" << config.name
                      << "] Using auto-detected Chrome path: " << config.browser_path << "\n";
        } else {
            config.browser_path = "C:\\Program Files\\Google\\Chrome\\Application\\chrome.exe";
            std::cerr << "[Session:" << config.name << "] Failed to detect Chrome path.\n";
            std::cout << "[Session:" << config.name
                      << "] Defaulting to: " << config.browser_path << "\n";
        }
    }

    if (!std::filesystem::exists(config.browser_path)) {
        std::cerr << "[Session:" << config.name
                  << "] Chrome path does not exist: " << config.browser_path << "\n";
        return false;
    }

    std::string cmdLine = "\"" + config.browser_path + "\"";
    cmdLine += " --remote-debugging-port=" + config.port;

    if (config.user_data_dir.empty()) {
        config.user_data_dir = ".\\temp_profile";
    }

    try {
        auto abs_path = std::filesystem::absolute(config.user_data_dir);
        if (!std::filesystem::exists(abs_path)) {
            std::filesystem::create_directories(abs_path);
        }
        config.user_data_dir = abs_path.string();
    } catch (const std::exception& e) {
        std::cerr << "[Session:" << config.name << "] Failed to create data dir: " << e.what() << "\n";
    }
    
    std::cout << "[Session:" << config.name
              << "] Using user data dir: " << config.user_data_dir << "\n";

    cmdLine += " --user-data-dir=\"" + config.user_data_dir + "\"";
    cmdLine += " --remote-allow-origins=* --start-minimized"; // Updated per previous fixes

    if (!config.start_url.empty()) {
        cmdLine += " \"" + config.start_url + "\"";
    }

    std::cout << "[Session:" << config.name << "] Launching: " << cmdLine << "\n";

    STARTUPINFOA si;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_SHOWMINNOACTIVE;
    PROCESS_INFORMATION pi;
    ZeroMemory(&pi, sizeof(pi));

    if (!CreateProcessA(nullptr, const_cast<char *>(cmdLine.c_str()), nullptr, nullptr, FALSE,
                        CREATE_NEW_CONSOLE, nullptr, nullptr, &si, &pi)) {
        std::cerr << "[Session:" << config.name
                  << "] Failed to launch. Error: " << GetLastError() << "\n";
        return false;
    }

    AssignProcessToJobForCleanup(pi.hProcess);

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    launched_ports.insert(config.port);
    std::cout << "[Session:" << config.name << "] Browser launched\n";
    return true;
}

bool BrowserAISession::Connect() {
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

bool BrowserAISession::Reconnect() {
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

bool BrowserAISession::InjectScript() {
    if (!connection || config.script_path.empty()) {
        return false;
    }

    return connection->InjectScript(config.script_path);
}

bool BrowserAISession::SendPrompt(const std::string &prompt, std::string &result,
                const std::string &extract_tag) {
    std::lock_guard<std::mutex> lock(session_mutex);

    if (!connection) {
        connection = std::make_unique<CDPConnection>(config.port, config.url_pattern);
    }

    if (!connection->EnsureConnected()) {
        return false;
    }

    this->InjectScript();

    return connection->SendPrompt(config.function_name, prompt, result, extract_tag);
}

void BrowserAISession::Shutdown() {
    std::lock_guard<std::mutex> lock(session_mutex);

    if (connection) {
        connection->Disconnect();
        connection.reset();
    }
}
