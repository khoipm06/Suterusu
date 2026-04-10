#include "browser_manager.h"
#include <memory>
#include <mutex>

// ============================================================================
// Global instances
// ============================================================================

static std::unique_ptr<CDPConnection> g_main_cdp; // Main browser (port 27245)
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

extern "C" __declspec(dllexport) bool InjectJavaScript(const char *filename) {
    std::lock_guard<std::mutex> lock(g_main_mutex);
    if (!g_main_cdp) {
        g_main_cdp = std::make_unique<CDPConnection>("27245");
    }
    return g_main_cdp->InjectPersistentScript(filename);
}

// ============================================================================
// Browser AI Session exports
// ============================================================================

extern "C" __declspec(dllexport) bool SessionStart(const char *config_path,
                                                   const char *session_name = "") {
    g_session_manager.LoadConfig(config_path);
    if (!session_name || strlen(session_name) == 0) {
        return g_session_manager.LaunchAll();
    } else {
        return g_session_manager.LaunchSession(session_name);
    }
    return false;
}

extern "C" __declspec(dllexport) bool SessionReconnect(const char *name) {
    auto *session = g_session_manager.GetSession(name);
    return session ? session->Reconnect() : false;
}

extern "C" __declspec(dllexport) bool SessionInjectScript(const char *name) {
    auto *session = g_session_manager.GetSession(name);
    return session ? session->InjectScript() : false;
}

extern "C" __declspec(dllexport) bool SessionSend(const char *name, const char *prompt,
                                                  char *result_buffer, int buffer_size,
                                                  const char *extract_tag) {
    auto *session = g_session_manager.GetSession(name);
    if (!session)
        return false;

    std::string result;
    std::string tag = extract_tag ? extract_tag : "answer";

    if (!session->SendPrompt(prompt, result, tag)) {
        return false;
    }

    strncpy(result_buffer, result.c_str(), buffer_size - 1);
    result_buffer[buffer_size - 1] = '\0';
    return true;
}

extern "C" __declspec(dllexport) void SessionEnd(const char *name) {
    auto *session = g_session_manager.GetSession(name);
    if (session)
        session->Shutdown();
}

extern "C" __declspec(dllexport) void SessionsShutdownAll() { g_session_manager.ShutdownAll(); }
