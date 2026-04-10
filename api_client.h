#ifndef API_CLIENT_H
#define API_CLIENT_H

#include <string>
#include <vector>
#include <mutex>
#include <nlohmann/json.hpp>
#include <curl/curl.h>

extern CURL *persistentCurl;
extern std::mutex curlMutex;
extern nlohmann::json chatHistory;
extern std::mutex historyMutex;

void InitializeCurl();
std::string SendToAPI(const std::string &prompt);
size_t WriteCallback(void *contents, size_t size, size_t nmemb, std::string *userp);
std::string ExtractTag(const std::string &text, const std::string &tag);

#endif // API_CLIENT_H