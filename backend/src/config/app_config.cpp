#include "config/app_config.hpp"

#include "common/file_utils.hpp"
#include "common/json_utils.hpp"

namespace facescan {

AppConfig::AppConfig()
    : backendPort(8080),
      databasePath("data/db/facescan.sqlite3"),
      imageRoot("data/images"),
      cameraMode("mock")
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
    if (body.empty()) {
        return config;
    }

    const int port = jsonIntValue(body, "backendPort");
    const std::string database = jsonStringValue(body, "database");
    const std::string imageRoot = jsonStringValue(body, "imageRoot");
    const std::string cameraMode = jsonStringValue(body, "cameraMode");
    if (port > 0 && port <= 65535) {
        config.backendPort = static_cast<unsigned short>(port);
    }
    if (!database.empty()) {
        config.databasePath = database;
    }
    if (!imageRoot.empty()) {
        config.imageRoot = imageRoot;
    }
    if (!cameraMode.empty()) {
        config.cameraMode = cameraMode;
    }

    const std::string backendRoot = backendRootFromConfigPath(configPath);
    config.databasePath = joinPath(backendRoot, config.databasePath);
    config.imageRoot = joinPath(backendRoot, config.imageRoot);
    return config;
}

} // namespace facescan
