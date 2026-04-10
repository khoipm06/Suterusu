#ifndef CDP_CONNECTION_H
#define CDP_CONNECTION_H

#include <boost/asio.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/websocket.hpp>
#include <nlohmann/json.hpp>

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

using tcp = boost::asio::ip::tcp;
namespace websocket = boost::beast::websocket;
using json = nlohmann::json;

struct TargetInfo {
    std::string id;
    std::string title;
    std::string url;
    std::string type;
    std::string webSocketDebuggerUrl;
};

class CDPConnection {
  private:
    std::unique_ptr<boost::asio::io_context> ioc;
    std::unique_ptr<websocket::stream<tcp::socket>> ws;
    std::mutex ws_mutex;
    std::atomic<bool> connected{false};
    std::atomic<int> message_id{1};
    std::string ws_url;
    std::string debug_port;
    std::string url_pattern;
    std::string connected_target_id;

    static std::string ReadFileContent(const std::string &script_path,
                                       const std::string &debug_port);

    std::vector<TargetInfo> QueryTargets();
    TargetInfo FindMatchingTarget(const std::vector<TargetInfo> &targets);
    bool ConnectToWebSocket(const std::string &ws_url_full);
    bool EstablishConnection();

  public:
    CDPConnection(const std::string &port, const std::string &pattern = "");
    ~CDPConnection();

    void SetUrlPattern(const std::string &pattern);
    std::string GetPort() const;

    bool EnsureConnected();
    bool SendCommand(const std::string &method, const json &params, std::string &response);
    bool InjectScript(const std::string &script_path);
    bool InjectPersistentScript(const std::string &script_path);
    bool SendPrompt(const std::string &function_name, const std::string &text, std::string &result,
                    const std::string &extract_tag = "");
    void Disconnect();
    bool ForceReconnect();
};

#endif // CDP_CONNECTION_H
