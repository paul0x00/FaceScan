#pragma once

#include "FaceScanConfig/app_config.hpp"
#include "common/module_api.hpp"

#include <boost/beast/http.hpp>

#include <memory>

namespace facescan {

/// HTTP API 应用层，负责路由请求、调用服务并生成响应。
class FACESCAN_SERVICE_API App {
public:
    /// 使用运行配置构造应用实例。
    explicit App(const AppConfig& config);
    /// 释放隐藏实现和其持有的资源。
    ~App();

    /// 处理单个 HTTP 请求并返回完整响应。
    boost::beast::http::response<boost::beast::http::string_body> handle(
        const boost::beast::http::request<boost::beast::http::string_body>& req);

private:
    /// 隐藏实现，隔离 Boost/数据库/相机等实现细节。
    struct Impl;
    /// 应用实现实例。
    std::unique_ptr<Impl> impl_;
};

} // namespace facescan
