#include "FaceScanConfig/app_config.hpp"
#include "FaceScanService/http_server.hpp"

#include <cerrno>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {

/// 解析命令行端口；未提供时使用配置文件中的端口。
unsigned short resolvePort(int argc, char* argv[], unsigned short configuredPort)
{
    if (argc <= 1) {
        return configuredPort;
    }

    char* end = nullptr;
    errno = 0;
    const long value = std::strtol(argv[1], &end, 10);
    if (errno != 0 || end == argv[1] || *end != '\0' || value <= 0
        || value > std::numeric_limits<unsigned short>::max()) {
        throw std::runtime_error("Invalid backend port: " + std::string(argv[1]));
    }
    return static_cast<unsigned short>(value);
}

} // namespace

/// 后端进程入口，负责配置加载、端口解析和服务生命周期启动。
int main(int argc, char* argv[])
{
    try {
        const facescan::AppConfig config = facescan::loadAppConfig();
        const unsigned short port = resolvePort(argc, argv, config.backendPort);
        return facescan::runBackend(config, port);
    } catch (const std::exception& e) {
        std::cerr << "Backend failed: " << e.what() << std::endl;
        return 1;
    }
}
