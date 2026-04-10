#include "overlay.h"
#include "config_manager.h"
#include "api_client.h"
#include "sys_utils.h"
#include "app_process.h"
#include "cdp_connection.h"

#include <iostream>
#include <windows.h>
#include <curl/curl.h>

// CDP injector DLL functions
extern "C" __declspec(dllimport) bool InitializeCDP();
extern "C" __declspec(dllimport) void ShutdownCDP();
extern "C" __declspec(dllimport) void SessionsShutdownAll();
extern "C" __declspec(dllimport) bool SessionStart(const char *config_path,
                                                   const char *session_name = "");

BOOL WINAPI ConsoleHandler(DWORD signal) {
    if (signal == CTRL_C_EVENT || signal == CTRL_BREAK_EVENT || signal == CTRL_CLOSE_EVENT) {
        std::cout << "\n[DEBUG] Console close signal received, cleaning up..." << std::endl;
        programRunning = false;
        Sleep(100);
        UnhookWindowsHookEx(hKeyboardHook);

        // Clean up persistent CURL handle
        if (persistentCurl) {
            curl_easy_cleanup(persistentCurl);
            persistentCurl = nullptr;
        }
        curl_global_cleanup();
        return TRUE;
    }
    return FALSE;
}

void ExitHandler() {
    std::cout << "[DEBUG] Program is terminating! This should not happen during normal operation" << std::endl;
    std::cout.flush();
}

class ConsoleStreamBuf : public std::streambuf {
    HANDLE hConsole;

  public:
    ConsoleStreamBuf(HANDLE h) : hConsole(h) {}

  protected:
    virtual int_type overflow(int_type c) override {
        if (c != EOF) {
            char ch = static_cast<char>(c);
            DWORD written;
            WriteConsoleA(hConsole, &ch, 1, &written, NULL);
        }
        return c;
    }
    virtual std::streamsize xsputn(const char *s, std::streamsize n) override {
        DWORD written;
        WriteConsoleA(hConsole, s, static_cast<DWORD>(n), &written, NULL);
        return n;
    }
};

int main(int argc, char *argv[]) {
    bool debugMode = false;
    for (int i = 1; i < argc; i++) {
        if (std::string(argv[i]) == "--debug") {
            debugMode = true;
            break;
        }
    }
    if (debugMode) {
        if (AllocConsole()) {
            SetConsoleOutputCP(CP_UTF8);
            SetConsoleCP(CP_UTF8);
            HANDLE hConOut = CreateFileA("CONOUT$", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_WRITE,
                                         NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
            if (hConOut != INVALID_HANDLE_VALUE) {
                static ConsoleStreamBuf consoleBuf(hConOut);
                std::cout.rdbuf(&consoleBuf);
                std::cerr.rdbuf(&consoleBuf);
                std::clog.rdbuf(&consoleBuf);
                if (!freopen("CONOUT$", "w", stdout))
                    freopen("/dev/console", "w", stdout);
                if (!freopen("CONOUT$", "w", stderr))
                    freopen("/dev/console", "w", stderr);
                if (!freopen("CONIN$", "r", stdin))
                    freopen("/dev/console", "r", stdin);
                setvbuf(stdout, NULL, _IONBF, 0);
                setvbuf(stderr, NULL, _IONBF, 0);
                std::cout << "[DEBUG] Console allocated and streams redirected "
                             "via custom buffer."
                          << std::endl;
            }
        }
    }
    std::atexit(ExitHandler);
    if (debugMode)
        SetConsoleCtrlHandler(ConsoleHandler, TRUE);
    std::cout << "=== Suterusu ===" << std::endl;
    std::cout << "Loading configuration from config.json..." << std::endl;
    if (!LoadConfig()) {
        std::cerr << "Failed to load configuration. Exiting." << std::endl;
        return 1;
    }
    printf("Debug mode: %s\n", debugMode ? "Enabled" : "Disabled");
    std::cout << std::endl;
    std::cout << "Configuration loaded successfully:" << std::endl;
    std::cout << "  API URL: " << API_URL << std::endl;
    std::cout << "  Model: " << MODEL << std::endl;
    if (!FALLBACK_MODELS.empty()) {
        std::cout << "  Fallback Models: ";
        for (size_t i = 0; i < FALLBACK_MODELS.size(); ++i) {
            std::cout << FALLBACK_MODELS[i];
            if (i < FALLBACK_MODELS.size() - 1)
                std::cout << ", ";
        }
        std::cout << std::endl;
    }
    if (!AI_PROVIDERS.empty()) {
        std::cout << "  Providers: ";
        for (size_t i = 0; i < AI_PROVIDERS.size(); ++i) {
            std::cout << AI_PROVIDERS[i];
            if (i < AI_PROVIDERS.size() - 1)
                std::cout << ", ";
        }
        std::cout << std::endl;
    }
    std::cout << "  API Key: " << (API_KEY.empty() ? "(not set)" : "********") << std::endl;
    std::cout << "  System Prompt: "
              << (SYSTEM_PROMPT.empty()
                      ? "(default)"
                      : SYSTEM_PROMPT.substr(0, 50) + (SYSTEM_PROMPT.length() > 50 ? "..." : ""))
              << std::endl;
    std::cout << "  Flash Window: " << FLASH_WINDOW << std::endl;
    std::cout << std::endl;
    std::cout << "Controls:" << std::endl;
    std::cout << "  F4 - Refresh CDP scripts (reinject all startup scripts)" << std::endl;
    std::cout << "  F5 - Reload browser AI sessions config" << std::endl;
    std::cout << "  F6 - Clear chat history" << std::endl;
    std::cout << "  F7 - Read clipboard and send to API" << std::endl;
    std::cout << "  F8 - Replace clipboard with API response" << std::endl;
    std::cout << "  F9 - Toggle text selection (handled by JavaScript not "
                 "keyboard hook)"
              << std::endl;
    std::cout << "  F10 - Send clipboard to browser AI session (e.g., ChatGPT) "
                 "*EXPERIMENTAL*"
              << std::endl;
    std::cout << "  F12 - Quit application" << std::endl;
    std::cout << std::endl;
    std::cout << "Note: Additional hotkeys may be defined in userscripts" << std::endl;
    std::cout << std::endl;

    // Initialize CURL before showing ready message
    curl_global_init(CURL_GLOBAL_DEFAULT);
    InitializeCurl();

    // Initialize CDP connection FIRST (before loading scripts)
    std::cout << "Initializing Chrome DevTools connection..." << std::endl;
    if (InitializeCDP()) {
        std::cout << "[CDP] Connection established - ready for script injection" << std::endl;
    } else {
        std::cout << "[CDP] Warning: Connection failed - will retry when needed" << std::endl;
    }

    // Load and inject startup scripts (CDP must be initialized first)
    LoadStartupScripts();

    // Load browser AI sessions from config
    std::cout << "Loading browser AI sessions from config.json..." << std::endl;

    SessionStart("config.json");

    std::cout << "Waiting for key presses..." << std::endl;
    std::cout.flush();

    hKeyboardHook = SetWindowsHookEx(WH_KEYBOARD_LL, KeyboardProc, nullptr, 0);
    if (hKeyboardHook == nullptr) {
        std::cerr << "Failed to install keyboard hook!" << std::endl;
        curl_global_cleanup();
        return 1;
    }
    std::cout << "[DEBUG] Keyboard hook installed successfully" << std::endl;
    MSG msg;
    while (programRunning) {
        while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                programRunning = false;
                break;
            }
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        Sleep(10);
    }

    // Wait for all API threads to complete
    std::cout << "[DEBUG] Waiting for " << activeThreads.load() << " active thread(s) to finish..."
              << std::endl;
    while (activeThreads > 0)
        Sleep(100);
    UnhookWindowsHookEx(hKeyboardHook);

    // Clean up CDP connections
    ShutdownCDP();
    SessionsShutdownAll();

    // Clean up persistent CURL handle
    if (persistentCurl) {
        curl_easy_cleanup(persistentCurl);
        persistentCurl = nullptr;
    }
    curl_global_cleanup();
    return 0;
}
