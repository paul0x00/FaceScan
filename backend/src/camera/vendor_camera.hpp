#pragma once

#include "camera_manager.hpp"

#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace facescan {

/// 枚举到的厂商相机信息。
struct DiscoveredCamera {
    std::string vendor;
    std::string name;
    std::string serial;
    std::string connectionType;
};

/// 一台厂商相机在一次触发中产生的图像集合。
struct VendorTriggerShot {
    CameraFrameData color;
    CameraFrameData left;
    CameraFrameData right;
    std::string diagnostic;
    bool ok;

    VendorTriggerShot() : ok(false) {}
};

/// 多设备软触发屏障：所有设备准备完成后由管理线程统一释放。
class TriggerBarrier {
public:
    TriggerBarrier();

    void setExpected(int count);
    void arrive();
    bool waitAllReady(int timeoutMs);
    bool wait(int timeoutMs);
    void release();
    void cancelAndRelease();

private:
    std::mutex mutex_;
    std::condition_variable cv_;
    int expected_;
    int arrived_;
    bool released_;
    bool cancelled_;
};

/// 单台真实相机的统一接口，屏蔽 Orbbec、海康和迈德威视 SDK 差异。
class IVendorCamera {
public:
    virtual ~IVendorCamera();

    virtual const std::string& vendor() const = 0;
    virtual const std::string& name() const = 0;
    virtual const std::string& serial() const = 0;

    virtual bool startStream(bool triggerMode) = 0;
    virtual void stopStream() = 0;
    virtual bool streaming() const = 0;
    virtual CameraFrameData latestColorFrame() const = 0;
    virtual CameraControlState controls() = 0;
    virtual CameraControlState updateControls(const CameraControlUpdate& update) = 0;
    virtual bool captureTriggerSet(VendorTriggerShot& out, TriggerBarrier& barrier, int timeoutMs, bool firstInBatch) = 0;
};

/// 厂商设备枚举和按序列号打开接口。
class IVendorCameraBackend {
public:
    virtual ~IVendorCameraBackend();
    virtual const char* vendor() const = 0;
    virtual std::vector<DiscoveredCamera> discover(std::string& error) = 0;
    virtual std::shared_ptr<IVendorCamera> open(const std::string& serial, std::string& error) = 0;
};

/// 创建当前构建可用的厂商后端。
std::vector<std::shared_ptr<IVendorCameraBackend>> createVendorCameraBackends();

} // namespace facescan
