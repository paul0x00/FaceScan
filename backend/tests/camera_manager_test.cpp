#include "../src/FaceScanCamera/camera_manager.hpp"
#include "../src/FaceScanCamera/vendor_camera.hpp"

#include "../src/common/file_utils.hpp"
#include "test_utils.hpp"

#include <gtest/gtest.h>

#include <thread>

using namespace facescan;

/// 验证相机预览状态切换和模拟预览图输出。
TEST(CameraManagerTest, TracksStreamingStateAndReturnsPreviewFrame)
{
    facescan_test::ScopedTempDir temp("camera_preview");
    CameraManager camera(temp.path());

    EXPECT_FALSE(camera.streaming());

    camera.start();
    EXPECT_TRUE(camera.streaming());

    const CameraImage frame = camera.frameImage("front");
    EXPECT_EQ("image/svg+xml; charset=utf-8", frame.contentType);
    EXPECT_NE(std::string::npos, frame.body.find("<svg"));
    EXPECT_NE(std::string::npos, frame.body.find("正面相机图像"));
    EXPECT_NE(std::string::npos, frame.body.find("实时预览"));

    camera.stop();
    EXPECT_FALSE(camera.streaming());
}

/// 验证同步采集会写出四个视角的图像文件。
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

/// 验证模拟相机参数可读取、更新并按范围裁剪。
TEST(CameraManagerTest, UpdatesMockCameraControls)
{
    facescan_test::ScopedTempDir temp("camera_controls");
    CameraManager camera(temp.path());

    CameraControlState initial = camera.controls();
    EXPECT_TRUE(initial.autoExposureSupported);
    EXPECT_TRUE(initial.autoExposure);
    EXPECT_EQ(42, initial.exposure.value);
    EXPECT_EQ(12, initial.gain.value);
    EXPECT_EQ(56, initial.brightness.value);

    CameraControlUpdate update;
    update.hasAutoExposure = true;
    update.autoExposure = false;
    update.hasExposure = true;
    update.exposure = 500;
    update.hasGain = true;
    update.gain = 21;
    update.hasBrightness = true;
    update.brightness = -5;

    CameraControlState changed = camera.updateControls(update);
    EXPECT_FALSE(changed.autoExposure);
    EXPECT_EQ(100, changed.exposure.value);
    EXPECT_EQ(21, changed.gain.value);
    EXPECT_EQ(0, changed.brightness.value);
}

/// 验证多设备触发屏障会等待所有设备准备后统一释放。
TEST(CameraManagerTest, TriggerBarrierCoordinatesReadyWorkers)
{
    TriggerBarrier barrier;
    barrier.setExpected(2);
    bool firstReleased = false;
    bool secondReleased = false;
    std::thread first([&]() {
        barrier.arrive();
        firstReleased = barrier.wait(500);
    });
    std::thread second([&]() {
        barrier.arrive();
        secondReleased = barrier.wait(500);
    });

    EXPECT_TRUE(barrier.waitAllReady(500));
    barrier.release();
    first.join();
    second.join();
    EXPECT_TRUE(firstReleased);
    EXPECT_TRUE(secondReleased);
}

/// 验证取消屏障会立即唤醒设备线程并返回失败。
TEST(CameraManagerTest, TriggerBarrierCancellationWakesWorkers)
{
    TriggerBarrier barrier;
    barrier.setExpected(1);
    bool released = true;
    std::thread worker([&]() {
        barrier.arrive();
        released = barrier.wait(500);
    });
    EXPECT_TRUE(barrier.waitAllReady(500));
    barrier.cancelAndRelease();
    worker.join();
    EXPECT_FALSE(released);
}

/// 验证本轮屏障一旦取消，后续普通释放不能覆盖取消状态。
TEST(CameraManagerTest, TriggerBarrierReleaseDoesNotOverrideCancellation)
{
    TriggerBarrier barrier;
    barrier.setExpected(1);
    barrier.arrive();
    ASSERT_TRUE(barrier.waitAllReady(500));
    barrier.cancelAndRelease();
    barrier.release();
    EXPECT_FALSE(barrier.wait(10));
}
