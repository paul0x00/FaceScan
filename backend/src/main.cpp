#include "server/http_server.hpp"

/// 后端进程入口，转交 HTTP 服务启动逻辑。
int main(int argc, char* argv[])
{
    return facescan::runBackend(argc, argv);
}
