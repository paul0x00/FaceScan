#pragma once

#include <string>

namespace facescan {

struct AppConfig {
    AppConfig();

    unsigned short backendPort;
    std::string databasePath;
    std::string imageRoot;
    std::string cameraMode;
};

AppConfig loadAppConfig();

} // namespace facescan
