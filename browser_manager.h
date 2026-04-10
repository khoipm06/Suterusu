#ifndef BROWSER_MANAGER_H
#define BROWSER_MANAGER_H

#include "browser_session.h"
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <vector>

class BrowserAIManager {
  private:
    std::map<std::string, std::unique_ptr<BrowserAISession>> sessions;
    std::set<std::string> launched_ports;
    std::mutex manager_mutex;

  public:
    bool LoadConfig(const std::string &config_path);
    BrowserAISession *GetSession(const std::string &name);
    std::vector<std::string> GetSessionNames();
    bool LaunchAll();
    bool LaunchSession(const std::string &name);
    void ShutdownAll();
};

#endif // BROWSER_MANAGER_H