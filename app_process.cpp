#include "app_process.h"
#include "sys_utils.h"
#include "api_client.h"
#include "config_manager.h"
#include "overlay.h"
#include <iostream>
#include <thread>

HHOOK hKeyboardHook;
std::queue<std::string> responseQueue;
std::mutex responseMutex;
std::atomic<int> activeThreads(0);
std::atomic<bool> programRunning(true);

// CDP injector DLL functions
extern "C" __declspec(dllimport) bool InitializeCDP();
extern "C" __declspec(dllimport) void ShutdownCDP();
extern "C" __declspec(dllimport) bool ReconnectCDP();
extern "C" __declspec(dllimport) bool InjectJavaScript(const char *filename);

// Browser AI Session functions
extern "C" __declspec(dllimport) bool SessionStart(const char *config_path,
                                                   const char *session_name = "");
extern "C" __declspec(dllimport) bool SessionReconnect(const char *name);
extern "C" __declspec(dllimport) bool SessionInjectScript(const char *name);
extern "C" __declspec(dllimport) bool SessionSend(const char *name, const char *prompt,
                                                  char *result_buffer, int buffer_size,
                                                  const char *extract_tag);
extern "C" __declspec(dllimport) void SessionEnd(const char *name);
extern "C" __declspec(dllimport) void SessionsShutdownAll();

void LoadStartupScripts() {
    std::cout << "[INFO] Loading startup scripts from js/startup/..." << std::endl;

    WIN32_FIND_DATAA findData;
    HANDLE hFind = FindFirstFileA("js\\startup\\*.js", &findData);

    if (hFind == INVALID_HANDLE_VALUE) {
        std::cout << "[WARN] No startup scripts found in js/startup. Skipping "
                     "loading."
                  << std::endl;
        return;
    }

    int loaded = 0;
    do {
        if (!(findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
            std::string filePath = "js\\startup\\";
            filePath += findData.cFileName;

            std::cout << "[INFO] Loading: " << findData.cFileName << std::endl;
            if (InjectJavaScript(filePath.c_str())) {
                loaded++;
            } else {
                std::cerr << "[ERROR] Failed to load: " << findData.cFileName << std::endl;
            }
        }
    } while (FindNextFileA(hFind, &findData));

    FindClose(hFind);
    std::cout << "[INFO] Loaded " << loaded << " startup script(s)" << std::endl;
}

LRESULT CALLBACK KeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode < 0 || wParam != WM_KEYDOWN)
        return CallNextHookEx(hKeyboardHook, nCode, wParam, lParam);
    DWORD vk = ((KBDLLHOOKSTRUCT *)lParam)->vkCode;
    switch (vk) {
    case VK_F4:
        std::cout << "F4 pressed - Reconnecting to CDP and refreshing scripts..." << std::endl;
        if (ReconnectCDP()) {
            std::cout << "[CDP] Reconnected to new target" << std::endl;
            LoadStartupScripts(); // Reinject all startup scripts
            std::cout << "CDP scripts refreshed." << std::endl;
            ShowOverlayIndicator(1000, IndicatorColor::Green);
        } else {
            std::cout << "[CDP] Failed to connect" << std::endl;
            ShowOverlayIndicator(1000, IndicatorColor::Red);
        }
        break;
    case VK_F6: {
        std::lock_guard<std::mutex> lock(historyMutex);
        chatHistory.clear();
        std::cout << "Chat history cleared." << std::endl;
        break;
    }
    case VK_F7: {
        std::cout << "F7 pressed - Processing..." << std::endl;
        std::string clipboardText = GetClipboardText();
        if (clipboardText.empty()) {
            std::cout << "Clipboard empty." << std::endl;
            break;
        }
        // Show green indicator when clipboard read succeeds and sending to API
        ShowOverlayIndicator(700, IndicatorColor::Green);

        // Launch thread for API request
        std::thread([clipboardText]() {
            activeThreads++;
            try {
                std::string response = SendToAPI(clipboardText);

                // Add response to queue
                {
                    std::lock_guard<std::mutex> lock(responseMutex);
                    responseQueue.push(response);
                    std::cout << "Response queued. Press F8 to copy (" << responseQueue.size()
                              << " pending)." << std::endl;
                }

                // Show green indicator with first character of response
                const char *firstChar = response.empty() ? nullptr : response.c_str();
                ShowOverlayIndicator(3000, IndicatorColor::Green, firstChar);
                FlashConfiguredWindows();
            } catch (...) {
                std::cerr << "Error in API thread." << std::endl;
            }
            activeThreads--;
        }).detach();
        break;
    }
    case VK_F8: {
        std::lock_guard<std::mutex> lock(responseMutex);
        if (!responseQueue.empty()) {
            std::string response = responseQueue.front();
            responseQueue.pop();

            std::cout << "Popped oldest response from queue. " << responseQueue.size()
                      << " response(s) remaining" << std::endl;

            if (SetClipboardText(response)) {
                std::cout << "Clipboard updated." << std::endl;
                // Show green indicator on successful clipboard update
                const char *firstChar = response.empty() ? nullptr : response.c_str();
                ShowOverlayIndicator(1000, IndicatorColor::Green, firstChar);
            } else {
                std::cout << "Failed to update clipboard." << std::endl;
            }
        } else {
            std::cout << "No response available." << std::endl;
            // Show red indicator when no response is available
            ShowOverlayIndicator(1000, IndicatorColor::Red);
        }
        break;
    }
    case VK_F5: {
        std::cout << "F5 pressed - Reloading browser AI sessions config..." << std::endl;
        if (SessionStart("config.json")) {
            std::cout << "Browser AI config reloaded successfully." << std::endl;
            ShowOverlayIndicator(1000, IndicatorColor::Green);
        } else {
            std::cout << "Failed to reload browser AI config." << std::endl;
            ShowOverlayIndicator(1000, IndicatorColor::Red);
        }
        break;
    }
    case VK_F10: {
        std::cout << "F10 pressed - Sending to ChatGPT session..." << std::endl;
        std::string clipboardText = GetClipboardText();
        if (clipboardText.empty()) {
            std::cout << "Clipboard empty." << std::endl;
            break;
        }

        ShowOverlayIndicator(700, IndicatorColor::Green);

        // Launch thread for session request
        std::thread([clipboardText]() {
            activeThreads++;
            try {
                const int BUFFER_SIZE = 51200;
                char *resultBuffer = new char[BUFFER_SIZE];

                if (SessionSend("chatgpt", clipboardText.c_str(), resultBuffer, BUFFER_SIZE,
                                "answer")) {
                    std::string response(resultBuffer);

                    {
                        std::lock_guard<std::mutex> lock(responseMutex);
                        responseQueue.push(response);
                        std::cout << "ChatGPT response queued. Press F8 to copy ("
                                  << responseQueue.size() << " pending)." << std::endl;
                    }

                    ShowOverlayIndicator(3000, IndicatorColor::Green);
                    FlashConfiguredWindows();
                } else {
                    std::cerr << "Failed to get ChatGPT response" << std::endl;
                    ShowOverlayIndicator(2000, IndicatorColor::Red);
                }

                delete[] resultBuffer;
            } catch (...) {
                std::cerr << "Error in session thread." << std::endl;
                ShowOverlayIndicator(2000, IndicatorColor::Red);
            }
            activeThreads--;
        }).detach();
        break;
    }
    // Use F12 to quit the application to prevent accidental closure
    case VK_F12:
        programRunning = false;
        PostQuitMessage(0);
        break;
    }
    return CallNextHookEx(hKeyboardHook, nCode, wParam, lParam);
}