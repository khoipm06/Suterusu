#include "cdp_connection.h"

#include <fstream>
#include <iostream>
#include <regex>

namespace http = boost::beast::http;

std::string CDPConnection::ReadFileContent(const std::string &script_path,
                                   const std::string &debug_port) {
    std::ifstream file(script_path);
    if (!file.is_open()) {
        std::cerr << "[CDP:" << debug_port << "] Cannot open: " << script_path << "\n";
        return "";
    }
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

std::vector<TargetInfo> CDPConnection::QueryTargets() {
    std::vector<TargetInfo> targets;

    try {
        const char *host = "127.0.0.1";
        const char *port = debug_port.c_str();

        if (!ioc) {
            ioc = std::make_unique<boost::asio::io_context>();
        }

        tcp::resolver resolver{*ioc};
        auto const results = resolver.resolve(host, port);

        boost::beast::tcp_stream http_stream{*ioc};
        http_stream.connect(results);

        http::request<http::string_body> req{http::verb::get, "/json", 11};
        req.set(http::field::host, host);
        req.set(http::field::user_agent,
                "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36");
        http::write(http_stream, req);

        boost::beast::flat_buffer buffer;
        http::response<http::string_body> res;
        http::read(http_stream, buffer, res);
        http_stream.socket().shutdown(tcp::socket::shutdown_both);

        auto j = json::parse(res.body());
        if (!j.is_array())
            return targets;

        for (auto &entry : j) {
            TargetInfo info;
            if (entry.contains("id"))
                info.id = entry["id"].get<std::string>();
            if (entry.contains("title"))
                info.title = entry["title"].get<std::string>();
            if (entry.contains("url"))
                info.url = entry["url"].get<std::string>();
            if (entry.contains("type"))
                info.type = entry["type"].get<std::string>();
            if (entry.contains("webSocketDebuggerUrl"))
                info.webSocketDebuggerUrl = entry["webSocketDebuggerUrl"].get<std::string>();

            if (info.type == "page" && !info.webSocketDebuggerUrl.empty()) {
                targets.push_back(info);
            }
        }

    } catch (std::exception const &e) {
        std::cerr << "[CDP:" << debug_port << "] QueryTargets error: " << e.what() << "\n";
    }

    return targets;
}

TargetInfo CDPConnection::FindMatchingTarget(const std::vector<TargetInfo> &targets) {
    if (url_pattern.empty() && !targets.empty()) {
        return targets[0]; // Return first target if no filter
    }

    try {
        std::regex pattern(url_pattern, std::regex::icase);
        for (const auto &target : targets) {
            if (std::regex_search(target.url, pattern)) {
                return target;
            }
        }
    } catch (std::regex_error &e) {
        std::cerr << "[CDP:" << debug_port << "] Invalid regex pattern: " << e.what() << "\n";
    }

    return {}; // Empty target
}

bool CDPConnection::ConnectToWebSocket(const std::string &ws_url_full) {
    try {
        // Here we assume che*ting on local machine, what is the purpose of
        // remoting to other host, right?
        const char *host = "127.0.0.1";

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
                {"id", message_id++}, {"method", "Page.enable"}, {"params", json::object()}};
            ws->write(boost::asio::buffer(enable_msg.dump()));
            boost::beast::flat_buffer enable_buffer;
            ws->read(enable_buffer);
        } catch (...) {
        }

        return true;

    } catch (std::exception const &e) {
        std::cerr << "[CDP:" << debug_port << "] WebSocket error: " << e.what() << "\n";
        connected = false;
        return false;
    }
}

bool CDPConnection::EstablishConnection() {
    ioc = std::make_unique<boost::asio::io_context>();

    auto targets = QueryTargets();
    if (targets.empty()) {
        std::cerr << "[CDP:" << debug_port << "] No targets found\n";
        return false;
    }

    auto target = FindMatchingTarget(targets);
    if (target.webSocketDebuggerUrl.empty()) {
        std::cerr << "[CDP:" << debug_port
                  << "] No matching target for pattern: " << url_pattern << "\n";
        std::cerr << "[CDP:" << debug_port << "] Available targets:\n";
        for (const auto &t : targets) {
            std::cerr << "  - " << t.title << " | " << t.url << "\n";
        }
        return false;
    }

    std::cout << "[CDP:" << debug_port << "] Connecting to: " << target.title << "\n";
    connected_target_id = target.id;
    return ConnectToWebSocket(target.webSocketDebuggerUrl);
}

CDPConnection::CDPConnection(const std::string &port, const std::string &pattern)
    : debug_port(port), url_pattern(pattern) {}

CDPConnection::~CDPConnection() { Disconnect(); }

void CDPConnection::SetUrlPattern(const std::string &pattern) { url_pattern = pattern; }

std::string CDPConnection::GetPort() const { return debug_port; }

bool CDPConnection::EnsureConnected() {
    std::lock_guard<std::mutex> lock(ws_mutex);

    if (connected && ws && ws->next_layer().is_open()) {
        return true;
    }

    std::cout << "[CDP:" << debug_port << "] Establishing connection...\n";
    return EstablishConnection();
}

bool CDPConnection::SendCommand(const std::string &method, const json &params, std::string &response) {
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

        int current_id = message_id++;
        json msg = {{"id", current_id}, {"method", method}, {"params", params}};

        ws->write(boost::asio::buffer(msg.dump()));

        // Keep reading until getting a response matching our command's ID
        while (true) {
            boost::beast::flat_buffer ws_buffer;
            ws->read(ws_buffer);
            response = boost::beast::buffers_to_string(ws_buffer.data());

            auto parsed = json::parse(response, nullptr, false);
            if (!parsed.is_discarded() && parsed.contains("id") && parsed["id"] == current_id) {
                break;
            }
        }

        return true;

    } catch (std::exception const &e) {
        std::cerr << "[CDP:" << debug_port << "] Send error: " << e.what() << "\n";
        connected = false;
        // Try to close the socket gracefully
        try {
            if (ws && ws->next_layer().is_open()) {
                ws->next_layer().close();
            }
        } catch (...) {
        }
        return false;
    }
}

// Inject script for one-time execution (Runtime.evaluate)
bool CDPConnection::InjectScript(const std::string &script_path) {
    if (!EnsureConnected())
        return false;

    try {
        std::string jsCode = ReadFileContent(script_path, debug_port);
        if (jsCode.empty())
            return false;

        json eval_params = {
            {"expression", jsCode}, {"userGesture", true}, {"awaitPromise", false}};

        std::string response;
        if (!SendCommand("Runtime.evaluate", eval_params, response)) {
            return false;
        }

        std::cout << "[CDP:" << debug_port << "] RAW InjectScript response: " << response << "\n";

        auto resp_json = json::parse(response);
        if (resp_json.contains("result") && resp_json["result"].contains("exceptionDetails")) {
            std::cerr << "[CDP:" << debug_port << "] JS Injection error in " << script_path << " :\n" 
                      << resp_json["result"]["exceptionDetails"].dump() << "\n";
            return false;
        }

        std::cout << "[CDP:" << debug_port << "] Script injected: " << script_path << "\n";
        return true;

    } catch (std::exception const &e) {
        std::cerr << "[CDP:" << debug_port << "] InjectScript error: " << e.what() << "\n";
        return false;
    }
}

// Inject script persistently (runs on every page load)
bool CDPConnection::InjectPersistentScript(const std::string &script_path) {
    if (!EnsureConnected())
        return false;

    try {
        std::string jsCode = ReadFileContent(script_path, debug_port);
        if (jsCode.empty())
            return false;

        // Register for future page loads
        json params = {{"source", jsCode}};
        std::string response;
        if (!SendCommand("Page.addScriptToEvaluateOnNewDocument", params, response)) {
            return false;
        }

        // Also evaluate immediately
        json eval_params = {
            {"expression", jsCode}, {"userGesture", true}, {"awaitPromise", false}};
        SendCommand("Runtime.evaluate", eval_params, response);

        std::cout << "[CDP:" << debug_port << "] Persistent script: " << script_path << "\n";
        return true;

    } catch (std::exception const &e) {
        std::cerr << "[CDP:" << debug_port << "] InjectPersistentScript error: " << e.what()
                  << "\n";
        return false;
    }
}

// Call a JavaScript function and get result
bool CDPConnection::SendPrompt(const std::string &function_name, const std::string &text, std::string &result,
                const std::string &extract_tag) {
    if (!EnsureConnected())
        return false;

    try {
        std::string jsCall = "window." + function_name + "(" + json(text).dump() + ")";

        json eval_params = {{"expression", jsCall},
                            {"userGesture", true},
                            {"awaitPromise", true},
                            {"returnByValue", true}};

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

                std::cout << "[CDP:" << debug_port << "] Got response (" << result.length()
                          << " chars)\n";
                return true;

            } else if (res.contains("exceptionDetails")) {
                std::cerr << "[CDP:" << debug_port
                          << "] JS error: " << res["exceptionDetails"].dump() << "\n";
                return false;
            }
        }

        std::cerr << "[CDP:" << debug_port << "] Unexpected response format\n";
        return false;

    } catch (std::exception const &e) {
        std::cerr << "[CDP:" << debug_port << "] SendPrompt error: " << e.what() << "\n";
        return false;
    }
}

void CDPConnection::Disconnect() {
    std::lock_guard<std::mutex> lock(ws_mutex);

    if (ws && connected) {
        try {
            ws->close(websocket::close_code::normal);
        } catch (...) {
        }
    }

    ws.reset();
    connected = false;
    connected_target_id.clear();
    std::cout << "[CDP:" << debug_port << "] Disconnected\n";
}

bool CDPConnection::ForceReconnect() {
    std::lock_guard<std::mutex> lock(ws_mutex);

    if (ws && connected) {
        try {
            ws->close(websocket::close_code::normal);
        } catch (...) {
        }
        ws.reset();
    }

    connected = false;
    ws_url.clear();
    connected_target_id.clear();

    return EstablishConnection();
}