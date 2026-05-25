#include "app_config.hpp"

#include "../common/file_utils.hpp"
#include "../common/json_utils.hpp"

#include <fstream>

namespace facescan {

namespace {

std::string configValueForFile(const std::string& configPath, const std::string& value)
{
    const std::string root = backendRootFromConfigPath(configPath);
    if (!root.empty() && root != "." && value.find(root + "/") == 0) {
        return value.substr(root.size() + 1);
    }
    return value;
}

std::string resolveConfigPath(const std::string& backendRoot, const std::string& value)
{
    if (!backendRoot.empty() && backendRoot != "." && value.find(backendRoot + "/") == 0) {
        return value;
    }
    return joinPath(backendRoot, value);
}

std::string fixedDatabasePath(const std::string& backendRoot)
{
    return joinPath(backendRoot, "data/db/facescan.sqlite3");
}

} // namespace

AppConfig::AppConfig()
    : backendPort(8080),
      databasePath("backend/data/db/facescan.sqlite3"),
      imageRoot("backend/data/images"),
      cameraMode("mock"),
      configPath("backend/config/app.json")
{
}

AppConfig loadAppConfig()
{
    AppConfig config;
    std::string configPath;
    std::string body;
    const char* candidates[] = {
        "config/app.json",
        "config/app.example.json",
        "backend/config/app.json",
        "backend/config/app.example.json"
    };
    for (std::size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); ++i) {
        body = readFileText(candidates[i]);
        if (!body.empty()) {
            configPath = candidates[i];
            break;
        }
    }
    if (!configPath.empty()) {
        const std::string configDir = parentDirectory(configPath);
        if (configPath.find(".example.json") != std::string::npos) {
            config.configPath = joinPath(configDir, "app.json");
        } else {
            config.configPath = configPath;
        }
    }
    if (body.empty()) {
        return config;
    }

    const int port = jsonIntValue(body, "backendPort");
    const std::string imageRoot = jsonStringValue(body, "imageRoot");
    const std::string cameraMode = jsonStringValue(body, "cameraMode");
    if (port > 0 && port <= 65535) {
        config.backendPort = static_cast<unsigned short>(port);
    }
    if (!imageRoot.empty()) {
        config.imageRoot = imageRoot;
    }
    if (!cameraMode.empty()) {
        config.cameraMode = cameraMode;
    }

    const std::string backendRoot = backendRootFromConfigPath(configPath);
    config.databasePath = fixedDatabasePath(backendRoot);
    config.imageRoot = resolveConfigPath(backendRoot, config.imageRoot);
    return config;
}

bool saveAppConfig(const AppConfig& config)
{
    ensureDirectory(parentDirectory(config.configPath));
    std::ofstream file(config.configPath.c_str(), std::ios::binary | std::ios::trunc);
    if (!file) {
        return false;
    }
    file << "{\n"
         << "  \"backendPort\": " << config.backendPort << ",\n"
         << "  \"database\": \"data/db/facescan.sqlite3\",\n"
         << "  \"imageRoot\": \"" << escapeJson(configValueForFile(config.configPath, config.imageRoot)) << "\",\n"
         << "  \"cameraMode\": \"" << escapeJson(config.cameraMode) << "\"\n"
         << "}\n";
    return true;
}

} // namespace facescan
