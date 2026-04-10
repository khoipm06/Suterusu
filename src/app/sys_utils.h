#ifndef SYS_UTILS_H
#define SYS_UTILS_H

#include <string>
#include <windows.h>

std::string GetClipboardText();
bool SetClipboardText(const std::string &text);
void FlashWindow(HWND hwnd);
BOOL CALLBACK EnumWindowsCallback(HWND hwnd, LPARAM lParam);
void FlashConfiguredWindows();

#endif // SYS_UTILS_H