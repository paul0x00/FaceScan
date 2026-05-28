#pragma once

namespace facescan {

/// 启动后端 HTTP 服务；可通过 argv[1] 覆盖配置端口。
int runBackend(int argc, char* argv[]);

} // namespace facescan
