#include "../src/FaceScanService/app.hpp"

#include "../src/common/bmp_utils.hpp"
#include "../src/common/file_utils.hpp"
#include "../src/common/json_utils.hpp"
#include "test_utils.hpp"

#include <boost/beast/http.hpp>
#include <gtest/gtest.h>

#include <fstream>

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

/// 验证点云编辑 API 在数据根目录内按压缩索引区间覆盖本地 PLY。
TEST(AppTest, PointCloudEditEndpointUpdatesLocalPly)
{
    facescan_test::ScopedTempDir temp("app_point_cloud_edit");
    AppConfig config;
    config.databasePath = temp.path() + "/db/facescan.sqlite3";
    config.imageRoot = temp.path() + "/images";
    config.configPath = temp.path() + "/config/app.json";
    ensureDirectory(config.imageRoot + "/order");
    const std::string plyPath = config.imageRoot + "/order/cloud.ply";
    std::ofstream out(plyPath.c_str(), std::ios::binary | std::ios::trunc);
    out << "ply\nformat ascii 1.0\nelement vertex 4\n"
        << "property float x\nproperty float y\nproperty float z\nend_header\n"
        << "0 0 0\n1 0 0\n2 0 0\n3 0 0\n";
    out.close();

    App app(config);
    http::request<http::string_body> req(http::verb::put, "/api/pointcloud/edit", 11);
    req.body() = "{\"path\":\"" + plyPath + "\",\"expectedPointCount\":4,\"deletedRanges\":[1,2]}";
    req.prepare_payload();

    const http::response<http::string_body> res = app.handle(req);

    ASSERT_EQ(http::status::ok, res.result()) << res.body();
    EXPECT_NE(std::string::npos, res.body().find("\"pointCount\":2"));
    const std::string body = readFileText(plyPath);
    EXPECT_NE(std::string::npos, body.find("element vertex 2\n"));
    EXPECT_NE(std::string::npos, body.find("0 0 0\n"));
    EXPECT_EQ(std::string::npos, body.find("1 0 0\n"));
    EXPECT_EQ(std::string::npos, body.find("2 0 0\n"));
    EXPECT_NE(std::string::npos, body.find("3 0 0\n"));

    const http::response<http::string_body> duplicateRes = app.handle(req);
    EXPECT_EQ(http::status::bad_request, duplicateRes.result());
    EXPECT_EQ(body, readFileText(plyPath));
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

/// 验证多相机配置具有约定默认值，并能完整保存到配置文件。
TEST(AppTest, SavesMultiCameraConfiguration)
{
    facescan_test::ScopedTempDir temp("app_multi_camera_config");
    AppConfig config;
    EXPECT_EQ("orbbec_parallel_then_module4", config.multiCameraTriggerWorkflow);
    EXPECT_EQ(1000, config.cameraTriggerTimeoutMs);

    config.configPath = temp.path() + "/config/app.json";
    config.cameraMode = "multi_camera";
    config.orbbecLeftSerial = "ORB-L";
    config.orbbecRightSerial = "ORB-R";
    config.orbbecBottomSerial = "ORB-B";
    config.hikvisionFrontSerial = "HIK-F";
    config.mindvisionStereoSerial = "MV-S";
    config.cameraTriggerTimeoutMs = 1500;

    ASSERT_TRUE(saveAppConfig(config));
    const std::string body = readFileText(config.configPath);
    EXPECT_NE(std::string::npos, body.find("\"cameraMode\": \"multi_camera\""));
    EXPECT_NE(std::string::npos, body.find("\"orbbecLeftSerial\": \"ORB-L\""));
    EXPECT_NE(std::string::npos, body.find("\"hikvisionFrontSerial\": \"HIK-F\""));
    EXPECT_NE(std::string::npos, body.find("\"mindvisionStereoSerial\": \"MV-S\""));
    EXPECT_NE(std::string::npos, body.find("\"cameraTriggerTimeoutMs\": 1500"));
}

/// 验证数据根反扫支持相机子目录 BMP，并保持拍摄页四视图顺序。
TEST(AppTest, RestoresCameraDirectoryBmpsAndServesBmpContentType)
{
    facescan_test::ScopedTempDir temp("app_camera_directory_bmp");
    AppConfig config;
    config.databasePath = temp.path() + "/db/facescan.sqlite3";
    config.imageRoot = temp.path() + "/images";
    config.configPath = temp.path() + "/config/app.json";

    const std::string orderRoot = config.imageRoot + "/P202607150001-Test/O20260715010101";
    const std::vector<unsigned char> rgb = { 10, 20, 30, 40, 50, 60 };
    const std::string orbbecLeft = orderRoot + "/left/O20260715010101_color.bmp";
    const std::string hikFront = orderRoot + "/front/O20260715010101_color.bmp";
    const std::string orbbecRight = orderRoot + "/right/O20260715010101_color.bmp";
    const std::string orbbecBottom = orderRoot + "/bottom/O20260715010101_color.bmp";
    const std::string mindvisionLeft = orderRoot + "/front/O20260715010101_left.bmp";
    ASSERT_TRUE(writeBmpRgb(mindvisionLeft, 2, 1, rgb));
    ASSERT_TRUE(writeBmpRgb(orbbecBottom, 2, 1, rgb));
    ASSERT_TRUE(writeBmpRgb(orbbecRight, 2, 1, rgb));
    ASSERT_TRUE(writeBmpRgb(hikFront, 2, 1, rgb));
    ASSERT_TRUE(writeBmpRgb(orbbecLeft, 2, 1, rgb));

    App app(config);
    http::request<http::string_body> patientsReq(http::verb::get, "/api/patients", 11);
    const http::response<http::string_body> patientsRes = app.handle(patientsReq);
    ASSERT_EQ(http::status::ok, patientsRes.result());
    const int patientId = jsonIntValue(patientsRes.body(), "id");
    ASSERT_GT(patientId, 0) << patientsRes.body();

    http::request<http::string_body> scansReq(
        http::verb::get,
        "/api/patients/" + std::to_string(patientId) + "/scans",
        11);
    const http::response<http::string_body> scansRes = app.handle(scansReq);
    ASSERT_EQ(http::status::ok, scansRes.result());
    const std::string body = scansRes.body();
    const std::size_t leftPosition = body.find(orbbecLeft);
    const std::size_t frontPosition = body.find(hikFront, leftPosition == std::string::npos ? 0 : leftPosition);
    const std::size_t rightPosition = body.find(orbbecRight);
    const std::size_t bottomPosition = body.find(orbbecBottom);
    const std::size_t stereoPosition = body.find(mindvisionLeft);
    ASSERT_NE(std::string::npos, leftPosition) << body;
    ASSERT_NE(std::string::npos, frontPosition) << body;
    ASSERT_NE(std::string::npos, rightPosition) << body;
    ASSERT_NE(std::string::npos, bottomPosition) << body;
    ASSERT_NE(std::string::npos, stereoPosition) << body;
    EXPECT_LT(leftPosition, frontPosition);
    EXPECT_LT(frontPosition, rightPosition);
    EXPECT_LT(rightPosition, bottomPosition);
    EXPECT_LT(bottomPosition, stereoPosition);
    EXPECT_NE(std::string::npos, body.find("\"previewPath\":\"" + hikFront));

    http::request<http::string_body> imageReq(
        http::verb::get,
        "/api/files/image?path=" + hikFront,
        11);
    const http::response<http::string_body> imageRes = app.handle(imageReq);
    EXPECT_EQ(http::status::ok, imageRes.result());
    EXPECT_EQ("image/bmp", imageRes[http::field::content_type]);
    ASSERT_GE(imageRes.body().size(), 2u);
    EXPECT_EQ('B', imageRes.body()[0]);
    EXPECT_EQ('M', imageRes.body()[1]);
}
