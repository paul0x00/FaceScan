#pragma once

#include "../common/module_api.hpp"

#include <memory>
#include <stdint.h>
#include <string>
#include <vector>

namespace facescan {

/// 可直接返回给 HTTP 客户端的相机预览图。
struct CameraImage {
    /// 图片二进制或文本内容。
    std::string body;
    /// HTTP Content-Type。
    std::string contentType;

    /// 构造空 SVG 预览图容器。
    CameraImage() : contentType("image/svg+xml; charset=utf-8") {}
    /// 使用指定内容和 MIME 类型构造预览图。
    CameraImage(const std::string& imageBody, const std::string& imageContentType)
        : body(imageBody), contentType(imageContentType)
    {
    }
};

/// 单路相机帧数据，既可承载 RGB，也可承载红外灰度帧。
struct CameraFrameData {
    /// 流名称，如 color、leftIr、rightIr。
    std::string stream;
    /// SDK 或文件格式名。
    std::string format;
    /// 帧宽度，单位像素。
    uint32_t width;
    /// 帧高度，单位像素。
    uint32_t height;
    /// 设备帧序号。
    uint64_t frameIndex;
    /// 采集时间文本。
    std::string capturedAt;
    /// 原始帧字节。
    std::vector<unsigned char> data;

    /// 初始化为空帧。
    CameraFrameData() : width(0), height(0), frameIndex(0) {}
    /// 判断帧是否包含可写入的数据。
    bool valid() const { return width > 0 && height > 0 && !data.empty(); }
};

/// 一次同步采集中保存的彩色和红外帧集合。
struct SynchronizedCaptureFrames {
    /// 彩色帧。
    CameraFrameData color;
    /// 左红外帧。
    CameraFrameData leftIr;
    /// 右红外帧。
    CameraFrameData rightIr;
    /// 彩色帧保存路径。
    std::string colorPath;
    /// 左红外帧保存路径。
    std::string leftIrPath;
    /// 右红外帧保存路径。
    std::string rightIrPath;
    /// 同步采集清单路径。
    std::string manifestPath;
    /// 用于后续流程展示和点云重建的纹理图路径。
    std::vector<std::string> texturePaths;

    /// 同步采集是否拿到了三路必要帧。
    bool complete() const { return color.valid() && leftIr.valid() && rightIr.valid(); }
};

/// 单个相机参数的当前值、范围和可写状态。
struct CameraControlRange {
    std::string key;
    std::string label;
    bool supported;
    bool writable;
    int value;
    int min;
    int max;
    int step;
    int defaultValue;

    CameraControlRange()
        : supported(false), writable(false), value(0), min(0), max(100), step(1), defaultValue(0)
    {
    }
};

/// 拍摄页可调的相机参数快照。
struct CameraControlState {
    bool autoExposureSupported;
    bool autoExposureWritable;
    bool autoExposure;
    CameraControlRange exposure;
    CameraControlRange gain;
    CameraControlRange brightness;

    CameraControlState()
        : autoExposureSupported(false), autoExposureWritable(false), autoExposure(false)
    {
    }
};

/// 相机参数更新请求，使用 has* 字段区分缺省值和实际写入值。
struct CameraControlUpdate {
    bool hasAutoExposure;
    bool autoExposure;
    bool hasExposure;
    int exposure;
    bool hasGain;
    int gain;
    bool hasBrightness;
    int brightness;

    CameraControlUpdate()
        : hasAutoExposure(false),
          autoExposure(false),
          hasExposure(false),
          exposure(0),
          hasGain(false),
          gain(0),
          hasBrightness(false),
          brightness(0)
    {
    }
};

/// 相机设备抽象接口，屏蔽模拟设备、单台 Orbbec 和多厂商组合设备差异。
class FACESCAN_CAMERA_API ICameraDevice {
public:
    /// 释放设备资源。
    virtual ~ICameraDevice();

    /// 开启预览流。
    virtual void start() = 0;
    /// 停止预览流。
    virtual void stop() = 0;
    /// 返回当前预览流是否运行。
    virtual bool streaming() const = 0;
    /// 更新图片保存根目录。
    virtual void setImageRoot(const std::string& imageRoot) = 0;
    /// 读取当前可调相机参数。
    virtual CameraControlState controls() = 0;
    /// 写入相机参数并返回最新状态。
    virtual CameraControlState updateControls(const CameraControlUpdate& update) = 0;
    /// 生成指定视角的预览图。
    virtual CameraImage frameImage(const std::string& view) = 0;
    /// 同步采集并将订单图像写入指定目录。
    virtual std::vector<std::string> capture(const std::string& orderFolder, const std::string& orderName) = 0;
};


/// 多厂商相机模式的设备角色与触发配置。
struct MultiCameraConfig {
    /// 触发工作流：sequential_modules 或 orbbec_parallel_then_module4。
    std::string triggerWorkflow;
    /// 左侧 Orbbec 序列号，空值时按枚举顺序兜底。
    std::string orbbecLeftSerial;
    /// 右侧 Orbbec 序列号，空值时按枚举顺序兜底。
    std::string orbbecRightSerial;
    /// 下方 Orbbec 序列号，空值时按枚举顺序兜底。
    std::string orbbecBottomSerial;
    /// 正面海康彩色相机序列号，空值时使用第一台枚举设备。
    std::string hikvisionFrontSerial;
    /// 迈德威视双目相机序列号，空值时使用第一台枚举设备。
    std::string mindvisionStereoSerial;
    /// 单步触发等待超时，单位毫秒。
    int triggerTimeoutMs;

    MultiCameraConfig()
        : triggerWorkflow("orbbec_parallel_then_module4"), triggerTimeoutMs(1000)
    {
    }
};

/// 相机服务门面，向 API 层提供稳定的预览和采集能力。
class FACESCAN_CAMERA_API CameraManager {
public:
    /// 使用模拟相机创建管理器。
    explicit CameraManager(const std::string& imageRoot);
    /// 按配置选择模拟相机或真实设备。
    CameraManager(const std::string& imageRoot, const std::string& cameraMode);
    /// 按配置选择相机模式，并传入多厂商设备角色配置。
    CameraManager(const std::string& imageRoot, const std::string& cameraMode, const MultiCameraConfig& multiCameraConfig);
    /// 注入自定义设备实现，主要用于测试。
    explicit CameraManager(std::unique_ptr<ICameraDevice> device);

    /// 开启预览。
    void start();
    /// 停止预览。
    void stop();
    /// 查询预览是否运行。
    bool streaming() const;
    /// 更新图片保存根目录。
    void setImageRoot(const std::string& imageRoot);
    /// 读取当前可调相机参数。
    CameraControlState controls();
    /// 写入相机参数并返回最新状态。
    CameraControlState updateControls(const CameraControlUpdate& update);

    /// 规范化图片根目录，去掉末尾分隔符并提供默认值。
    static std::string normalizeRoot(const std::string& path);

    /// 获取指定视角的预览图。
    CameraImage frameImage(const std::string& view);
    /// 同步采集订单图像并返回写入路径。
    std::vector<std::string> capture(const std::string& orderFolder, const std::string& orderName);

private:
    /// 当前启用的相机设备实现。
    std::unique_ptr<ICameraDevice> device_;
};

} // namespace facescan
