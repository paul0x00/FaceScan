#pragma once

#include "common/module_api.hpp"
#include "FaceScanConfig/app_config.hpp"

namespace facescan {

/// 使用主程序提供的配置和端口启动后端 HTTP 服务。
FACESCAN_SERVICE_API int runBackend(const AppConfig& config, unsigned short port);

} // namespace facescan
