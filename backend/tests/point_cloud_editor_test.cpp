#include "../src/algorithm/point_cloud_editor.hpp"

#include "../src/common/file_utils.hpp"
#include "test_utils.hpp"

#include <gtest/gtest.h>

#include <fstream>
#include <string>
#include <vector>

using namespace facescan;

namespace {

/// 写入最小 ASCII 点云 PLY。
void writeTestPly(const std::string& path)
{
    std::ofstream out(path.c_str(), std::ios::binary | std::ios::trunc);
    out << "ply\n"
        << "format ascii 1.0\n"
        << "element vertex 5\n"
        << "property float x\n"
        << "property float y\n"
        << "property float z\n"
        << "property uchar red\n"
        << "property uchar green\n"
        << "property uchar blue\n"
        << "end_header\n"
        << "0 0 0 255 0 0\n"
        << "1 0 0 0 255 0\n"
        << "2 0 0 0 0 255\n"
        << "3 0 0 255 255 0\n"
        << "4 0 0 255 255 255\n";
}

} // namespace

/// 验证编辑器按原始索引删除顶点并更新 PLY 头部。
TEST(PointCloudEditorTest, RemovesVerticesAndUpdatesHeader)
{
    facescan_test::ScopedTempDir temp("point_cloud_edit");
    const std::string path = temp.path() + "/cloud.ply";
    writeTestPly(path);

    const PointCloudEditResult result = removePointCloudVertices(path, { 1, 3, 3 });

    ASSERT_TRUE(result.ok) << result.message;
    EXPECT_EQ(5u, result.originalPointCount);
    EXPECT_EQ(3u, result.pointCount);
    EXPECT_EQ(2u, result.removedPointCount);
    const std::string body = readFileText(path);
    EXPECT_NE(std::string::npos, body.find("element vertex 3\n"));
    EXPECT_NE(std::string::npos, body.find("0 0 0 255 0 0\n"));
    EXPECT_EQ(std::string::npos, body.find("1 0 0 0 255 0\n"));
    EXPECT_NE(std::string::npos, body.find("2 0 0 0 0 255\n"));
    EXPECT_EQ(std::string::npos, body.find("3 0 0 255 255 0\n"));
    EXPECT_NE(std::string::npos, body.find("4 0 0 255 255 255\n"));
}

/// 验证编辑器拒绝删除全部顶点，且不会修改源文件。
TEST(PointCloudEditorTest, RejectsRemovingEveryVertex)
{
    facescan_test::ScopedTempDir temp("point_cloud_edit_all");
    const std::string path = temp.path() + "/cloud.ply";
    writeTestPly(path);
    const std::string original = readFileText(path);

    const PointCloudEditResult result = removePointCloudVertices(path, { 0, 1, 2, 3, 4 });

    EXPECT_FALSE(result.ok);
    EXPECT_EQ(original, readFileText(path));
}

/// 验证点数版本不一致时拒绝覆盖，防止重复离开请求再次删除。
TEST(PointCloudEditorTest, RejectsStaleExpectedPointCount)
{
    facescan_test::ScopedTempDir temp("point_cloud_edit_stale");
    const std::string path = temp.path() + "/cloud.ply";
    writeTestPly(path);
    const std::string original = readFileText(path);

    const PointCloudEditResult result = removePointCloudVertices(path, { 1 }, 4);

    EXPECT_FALSE(result.ok);
    EXPECT_EQ(original, readFileText(path));
}
