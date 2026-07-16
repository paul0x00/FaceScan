#pragma once

#include <string>

namespace facescan {

/// 后端运行配置，来自 app.json 或内置默认值。
struct AppConfig {
    /// 构造默认配置。
    AppConfig();

    /// HTTP 监听端口。
    unsigned short backendPort;
    /// SQLite 数据库文件路径。
    std::string databasePath;
    /// 患者采图、点云和压缩包保存根目录。
    std::string imageRoot;
    /// 相机模式：mock、orbbec/gemini215 或 multi_camera。
    std::string cameraMode;
    /// 多相机触发工作流。
    std::string multiCameraTriggerWorkflow;
    /// 左侧 Orbbec 序列号。
    std::string orbbecLeftSerial;
    /// 右侧 Orbbec 序列号。
    std::string orbbecRightSerial;
    /// 下方 Orbbec 序列号。
    std::string orbbecBottomSerial;
    /// 正面海康彩色相机序列号。
    std::string hikvisionFrontSerial;
    /// 迈德威视双目相机序列号。
    std::string mindvisionStereoSerial;
    /// 厂商相机单步触发超时，单位毫秒。
    int cameraTriggerTimeoutMs;
    /// 当前用户配置文件路径。
    std::string configPath;
};

/// 从配置文件加载后端配置；缺失时返回默认配置。
AppConfig loadAppConfig();
/// 将可编辑配置写回 app.json。
bool saveAppConfig(const AppConfig& config);

} // namespace facescan
