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

/// 验证相机参数 API 可读取和更新。
TEST(AppTest, CameraControlsEndpointReadsAndUpdatesControls)
{
    facescan_test::ScopedTempDir temp("app_camera_controls");

    AppConfig config;
    config.databasePath = temp.path() + "/db/facescan.sqlite3";
    config.imageRoot = temp.path() + "/images";
    config.configPath = temp.path() + "/config/app.json";

    App app(config);
    http::request<http::string_body> getReq(http::verb::get, "/api/camera/controls", 11);
    const http::response<http::string_body> getRes = app.handle(getReq);

    EXPECT_EQ(http::status::ok, getRes.result());
    EXPECT_NE(std::string::npos, getRes.body().find("\"autoExposure\":true"));
    EXPECT_NE(std::string::npos, getRes.body().find("\"gain\""));

    http::request<http::string_body> putReq(http::verb::put, "/api/camera/controls", 11);
    putReq.body() = "{\"autoExposure\":false,\"exposure\":33,\"gain\":9,\"brightness\":44}";
    putReq.prepare_payload();
    const http::response<http::string_body> putRes = app.handle(putReq);

    EXPECT_EQ(http::status::ok, putRes.result());
    EXPECT_NE(std::string::npos, putRes.body().find("\"autoExposure\":false"));
    EXPECT_NE(std::string::npos, putRes.body().find("\"value\":33"));
    EXPECT_NE(std::string::npos, putRes.body().find("\"value\":9"));
    EXPECT_NE(std::string::npos, putRes.body().find("\"value\":44"));
}
