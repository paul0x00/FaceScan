#include "../src/api/app.hpp"

#include "test_utils.hpp"

#include <boost/beast/http.hpp>
#include <gtest/gtest.h>

using namespace facescan;
namespace http = boost::beast::http;

/// 验证健康检查接口返回 ok。
TEST(AppTest, HealthEndpointReturnsOk)
{
    facescan_test::ScopedTempDir temp("app_health");

    AppConfig config;
    config.databasePath = temp.path() + "/db/facescan.sqlite3";
    config.imageRoot = temp.path() + "/images";
    config.configPath = temp.path() + "/config/app.json";

    App app(config);
    http::request<http::string_body> req(http::verb::get, "/api/health", 11);

    const http::response<http::string_body> res = app.handle(req);

    EXPECT_EQ(http::status::ok, res.result());
    EXPECT_EQ("{\"status\":\"ok\"}", res.body());
    EXPECT_EQ("application/json; charset=utf-8", res[http::field::content_type]);
}

/// 验证未知接口返回 404。
TEST(AppTest, UnknownEndpointReturnsNotFound)
{
    facescan_test::ScopedTempDir temp("app_not_found");

    AppConfig config;
    config.databasePath = temp.path() + "/db/facescan.sqlite3";
    config.imageRoot = temp.path() + "/images";
    config.configPath = temp.path() + "/config/app.json";

    App app(config);
    http::request<http::string_body> req(http::verb::get, "/api/unknown", 11);

    const http::response<http::string_body> res = app.handle(req);

    EXPECT_EQ(http::status::not_found, res.result());
    EXPECT_EQ("{\"error\":\"not found\"}", res.body());
}
