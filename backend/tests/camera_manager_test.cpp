#include "camera/camera_manager.hpp"

#include "common/file_utils.hpp"
#include "tests/test_utils.hpp"

#include <gtest/gtest.h>

using namespace facescan;

TEST(CameraManagerTest, TracksStreamingStateAndReturnsPreviewFrame)
{
    facescan_test::ScopedTempDir temp("camera_preview");
    CameraManager camera(temp.path());

    EXPECT_FALSE(camera.streaming());

    camera.start();
    EXPECT_TRUE(camera.streaming());

    const std::string frame = camera.frameSvg("front");
    EXPECT_NE(std::string::npos, frame.find("<svg"));
    EXPECT_NE(std::string::npos, frame.find("正面相机图像"));
    EXPECT_NE(std::string::npos, frame.find("实时预览"));

    camera.stop();
    EXPECT_FALSE(camera.streaming());
}

TEST(CameraManagerTest, CaptureWritesFourViewFiles)
{
    facescan_test::ScopedTempDir temp("camera_capture");
    CameraManager camera(temp.path());

    const std::string orderFolder = temp.path() + "/order";
    const std::vector<std::string> paths = camera.capture(orderFolder, "ORDER001");

    ASSERT_EQ(4u, paths.size());
    EXPECT_NE(std::string::npos, paths[0].find("_left.svg"));
    EXPECT_NE(std::string::npos, paths[1].find("_front.svg"));
    EXPECT_NE(std::string::npos, paths[2].find("_right.svg"));
    EXPECT_NE(std::string::npos, paths[3].find("_bottom.svg"));

    for (std::size_t i = 0; i < paths.size(); ++i) {
        EXPECT_TRUE(pathExists(paths[i])) << paths[i];
        EXPECT_NE(std::string::npos, readFileText(paths[i]).find("<svg")) << paths[i];
    }
}
