#pragma once

#include "camera_manager.hpp"

#include <memory>

namespace facescan {

/// 创建 3 台 Orbbec + 迈德威视双目 + 海康彩色的组合相机设备。
FACESCAN_CAMERA_API std::unique_ptr<ICameraDevice> createMultiCameraDevice(
    const std::string& imageRoot,
    const MultiCameraConfig& config);

} // namespace facescan
