#include "api_client.h"
#include "config_manager.h"
#include <iostream>

using json = nlohmann::json;

CURL *persistentCurl = nullptr;
std::mutex curlMutex;

json chatHistory = json::array();
std::mutex historyMutex;

size_t WriteCallback(void *contents, size_t size, size_t nmemb, std::string *userp) {
    size_t totalSize = size * nmemb;
    userp->append((char *)contents, totalSize);
    return totalSize;
}

std::string ExtractTag(const std::string &text, const std::string &tag) {
    std::string openTag = "<" + tag + ">";
    std::string closeTag = "</" + tag + ">";

    size_t startPos = text.find(openTag);
    size_t endPos = text.find(closeTag);

    // If both open and close tags are found, extract content between them
    if (startPos != std::string::npos && endPos != std::string::npos && endPos > startPos) {
        startPos += openTag.length();
        return text.substr(startPos, endPos - startPos);
    }

    // Fallback to return the whole text if tags are not found
    return text;
}

void InitializeCurl() {
    std::lock_guard<std::mutex> curlLock(curlMutex);

    if (persistentCurl)
        return; // Already initialized

    persistentCurl = curl_easy_init();
    if (!persistentCurl) {
        std::cerr << "[ERROR] Failed to initialize CURL handle" << std::endl;
        return;
    }

    // LOW LATENCY OPTIMIZATIONS - Set once for the persistent handle

    // Enable HTTP/2 if available (multiplexing, better performance)
    curl_easy_setopt(persistentCurl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_2_0);

    // Disable Nagle's algorithm for lower latency (send small packets
    // immediately)
    curl_easy_setopt(persistentCurl, CURLOPT_TCP_NODELAY, 1L);

    // Enable connection reuse and pooling
    curl_easy_setopt(persistentCurl, CURLOPT_MAXCONNECTS, 5L);

    // Enable TCP keep-alive to maintain connections
    curl_easy_setopt(persistentCurl, CURLOPT_TCP_KEEPALIVE, 1L);
    curl_easy_setopt(persistentCurl, CURLOPT_TCP_KEEPIDLE, 60L);
    curl_easy_setopt(persistentCurl, CURLOPT_TCP_KEEPINTVL, 60L);

    // Set reduced connection timeout (5 seconds instead of 30)
    curl_easy_setopt(persistentCurl, CURLOPT_CONNECTTIMEOUT, 5L);

    // Set reasonable total timeout
    curl_easy_setopt(persistentCurl, CURLOPT_TIMEOUT, 120L);

    // Required for multi-threaded applications
    curl_easy_setopt(persistentCurl, CURLOPT_NOSIGNAL, 1L);

    // Enable DNS caching
    curl_easy_setopt(persistentCurl, CURLOPT_DNS_CACHE_TIMEOUT, 600L);

    // Use HTTP pipelining for better performance
    curl_easy_setopt(persistentCurl, CURLOPT_PIPEWAIT, 1L);

    /* Tells libcurl to use standard certificate store of operating system.
       Currently implemented under MS-Windows. */
    curl_easy_setopt(persistentCurl, CURLOPT_SSL_OPTIONS, CURLSSLOPT_NATIVE_CA);

    std::cout << "[DEBUG] Initialized persistent CURL handle with low-latency "
                 "optimizations"
              << std::endl;
}

std::string SendToAPI(const std::string &prompt) {
    std::lock_guard<std::mutex> curlLock(curlMutex);

    if (!persistentCurl)
        return "Error: CURL not initialized";

    std::string responseString;
    struct curl_slist *headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");

    // Enable HTTP keep-alive
    headers = curl_slist_append(headers, "Connection: keep-alive");

    if (!API_KEY.empty()) {
        std::string authHeader = "Authorization: Bearer " + API_KEY;
        headers = curl_slist_append(headers, authHeader.c_str());
    }

    json messages = json::array();
    if (!SYSTEM_PROMPT.empty())
        messages.push_back({{"role", "system"}, {"content", SYSTEM_PROMPT}});
    {
        std::lock_guard<std::mutex> lock(historyMutex);
        size_t historySize = chatHistory.size();
        // Keep last 20 messages (10 conversation pairs: user + assistant)
        size_t start = (historySize > 20) ? (historySize - 20) : 0;
        std::cout << "[DEBUG] Chat history size: " << historySize << " messages, using last "
                  << (historySize - start) << " messages" << std::endl;
        for (size_t i = start; i < historySize; ++i)
            messages.push_back(chatHistory[i]);
    }
    messages.push_back({{"role", "user"}, {"content", prompt}});

    // Build payload with optional provider routing and model fallbacks
    json payload = {{"model", MODEL}, {"messages", messages}, {"temperature", 0.7}};

    // Add model fallbacks if specified (for OpenRouter automatic failover)
    if (!FALLBACK_MODELS.empty()) {
        // Build fallback models array
        json modelsArray = json::array();
        for (const auto &fallbackModel : FALLBACK_MODELS) {
            modelsArray.push_back(fallbackModel);
        }
        payload["models"] = modelsArray;
        std::cout << "[DEBUG] Including model fallbacks: " << modelsArray.dump() << std::endl;
    }

    // Add provider routing if providers are specified (for OpenRouter)
    if (!AI_PROVIDERS.empty()) {
        payload["provider"] = {{"order", AI_PROVIDERS}, {"allow_fallbacks", true}};
        std::cout << "[DEBUG] Including provider routing: " << json(AI_PROVIDERS).dump()
                  << std::endl;
    }

    std::string jsonStr = payload.dump();

    // Set request-specific options
    curl_easy_setopt(persistentCurl, CURLOPT_URL, API_URL.c_str());
    curl_easy_setopt(persistentCurl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(persistentCurl, CURLOPT_POSTFIELDS, jsonStr.c_str());
    curl_easy_setopt(persistentCurl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(persistentCurl, CURLOPT_WRITEDATA, &responseString);

    CURLcode res = curl_easy_perform(persistentCurl);
    long response_code = 0;
    curl_easy_getinfo(persistentCurl, CURLINFO_RESPONSE_CODE, &response_code);

    // Clean up headers
    curl_slist_free_all(headers);

    // Don't cleanup the persistent handle - it will be reused

    if (res != CURLE_OK) {
        if (res == CURLE_OPERATION_TIMEDOUT)
            return "Error: Request timed out.";
        if (res == CURLE_COULDNT_CONNECT)
            return "Error: Could not connect to server.";
        if (res == CURLE_COULDNT_RESOLVE_HOST)
            return "Error: Could not resolve host.";
        return std::string("Error: ") + curl_easy_strerror(res);
    }
    try {
        json responseJson = json::parse(responseString);
        if (responseJson.contains("choices") && !responseJson["choices"].empty()) {
            std::string content = responseJson["choices"][0]["message"]["content"];
            // Assume the answer is wrapped in <answer> tags
            // User should specify this in the system prompt
            std::string finalAnswer = ExtractTag(content, "answer");
            {
                std::lock_guard<std::mutex> lock(historyMutex);
                chatHistory.push_back({{"role", "user"}, {"content", prompt}});
                chatHistory.push_back({{"role", "assistant"}, {"content", content}});
                std::cout << "[DEBUG] Added conversation to history. Total messages: "
                          << chatHistory.size() << std::endl;
            }
            return finalAnswer;
        } else if (responseJson.contains("error"))
            return "API Error: " + responseJson["error"]["message"].get<std::string>();
    } catch (const std::exception &e) {
        std::cout << "[DEBUG] Full API response: " << responseString << std::endl;
        return "Error parsing response: " + std::string(e.what());
    }
    std::cout << "[DEBUG] Full API response: " << responseString << std::endl;
    return "Error: Unexpected response format";
}