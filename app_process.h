#ifndef APP_PROCESS_H
#define APP_PROCESS_H

#include <windows.h>
#include <atomic>
#include <queue>
#include <mutex>
#include <string>

extern HHOOK hKeyboardHook;
extern std::queue<std::string> responseQueue;
extern std::mutex responseMutex;
extern std::atomic<int> activeThreads;
extern std::atomic<bool> programRunning;

void LoadStartupScripts();
LRESULT CALLBACK KeyboardProc(int nCode, WPARAM wParam, LPARAM lParam);

#endif // APP_PROCESS_H