#include "sys_utils.h"
#include "config_manager.h" // For FLASH_WINDOW
#include <algorithm>
#include <iostream>
#include <psapi.h>

void FlashWindow(HWND hwnd) {
    FLASHWINFO fwi;
    fwi.cbSize = sizeof(FLASHWINFO);
    fwi.hwnd = hwnd;
    fwi.dwFlags = FLASHW_TRAY;
    fwi.uCount = 3;
    fwi.dwTimeout = 0;
    FlashWindowEx(&fwi);
    Sleep(1600);
    fwi.dwFlags = FLASHW_STOP;
    fwi.uCount = 0;
    FlashWindowEx(&fwi);
}

BOOL CALLBACK EnumWindowsCallback(HWND hwnd, LPARAM lParam) {
    // Pretend to use lParam to suppress unused parameter warning
    (void)lParam;

    // Skip invisible windows early
    if (!IsWindowVisible(hwnd))
        return TRUE;

    // Get process ID from window handle
    DWORD processId = 0;
    GetWindowThreadProcessId(hwnd, &processId);

    if (processId == 0)
        return TRUE;

    // Open process with query permissions
    HANDLE hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
    if (hProcess == NULL)
        return TRUE;

    // Get executable path
    char exePath[MAX_PATH];
    DWORD size = MAX_PATH;
    if (QueryFullProcessImageNameA(hProcess, 0, exePath, &size) == 0) {
        CloseHandle(hProcess);
        return TRUE;
    }
    CloseHandle(hProcess);

    // Extract executable name from full path
    std::string fullPath = exePath;
    size_t lastSlash = fullPath.find_last_of("\\/");
    std::string exeName =
        (lastSlash != std::string::npos) ? fullPath.substr(lastSlash + 1) : fullPath;

    // Convert to lowercase for case-insensitive comparison
    std::transform(exeName.begin(), exeName.end(), exeName.begin(), ::tolower);

    std::string target = FLASH_WINDOW;
    std::transform(target.begin(), target.end(), target.begin(), ::tolower);

    bool shouldFlash = false;

    // Special cases
    if (target == "all") {
        shouldFlash = true;
    } else if (target == "none" || target.empty()) {
        shouldFlash = false;
    } else {
        // Check if target matches executable name (with or without .exe
        // extension)
        std::string targetWithExt = target;
        if (target.find(".exe") == std::string::npos)
            targetWithExt += ".exe";

        if (exeName == targetWithExt || exeName == target)
            shouldFlash = true;
        // Also support partial matching (e.g., "chrome" matches "chrome.exe")
        else if (exeName.find(target) != std::string::npos)
            shouldFlash = true;
    }

    if (shouldFlash) {
        char windowTitle[256];
        GetWindowTextA(hwnd, windowTitle, sizeof(windowTitle));
        std::cout << "[DEBUG] Found target window: " << windowTitle << " (Process: " << exeName
                  << ")" << std::endl;
        FlashWindow(hwnd);
        return FALSE;
    }

    return TRUE;
}

void FlashConfiguredWindows() {
    std::cout << "[DEBUG] Flashing '" << FLASH_WINDOW << "' windows..." << std::endl;
    EnumWindows(EnumWindowsCallback, 0);
}

std::string GetClipboardText() {
    if (!OpenClipboard(nullptr)) {
        std::cerr << "Failed to open clipboard" << std::endl;
        return "";
    }
    HANDLE hData = GetClipboardData(CF_UNICODETEXT);
    if (hData == nullptr) {
        CloseClipboard();
        return "";
    }
    wchar_t *pszText = static_cast<wchar_t *>(GlobalLock(hData));
    if (pszText == nullptr) {
        CloseClipboard();
        return "";
    }
    int size = WideCharToMultiByte(CP_UTF8, 0, pszText, -1, nullptr, 0, nullptr, nullptr);
    if (size <= 0) {
        std::cerr << "[DEBUG] Failed to get clipboard text size" << std::endl;
        GlobalUnlock(hData);
        CloseClipboard();
        return "";
    }
    std::string text(size - 1, 0);
    int result = WideCharToMultiByte(CP_UTF8, 0, pszText, -1, &text[0], size, nullptr, nullptr);
    GlobalUnlock(hData);
    CloseClipboard();
    return result == 0 ? "" : text;
}

bool SetClipboardText(const std::string &text) {
    if (!OpenClipboard(nullptr)) {
        std::cerr << "Failed to open clipboard" << std::endl;
        return false;
    }
    EmptyClipboard();
    int size = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
    HGLOBAL hGlob = GlobalAlloc(GMEM_MOVEABLE, size * sizeof(wchar_t));
    if (hGlob == nullptr) {
        CloseClipboard();
        return false;
    }
    wchar_t *pszText = static_cast<wchar_t *>(GlobalLock(hGlob));
    if (pszText == nullptr) {
        GlobalFree(hGlob);
        CloseClipboard();
        return false;
    }
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, pszText, size);
    GlobalUnlock(hGlob);
    SetClipboardData(CF_UNICODETEXT, hGlob);
    CloseClipboard();
    return true;
}