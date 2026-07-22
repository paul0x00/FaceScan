#pragma once

#include "vendor_camera.hpp"

namespace facescan {

/// 迈德威视 SDK 后端，目标设备输出左右目拼接图。
class MindvisionBackend : public IVendorCameraBackend {
public:
    MindvisionBackend();
    ~MindvisionBackend();

    const char* vendor() const { return "MindVision"; }
    std::vector<DiscoveredCamera> discover(std::string& error);
    std::shared_ptr<IVendorCamera> open(const std::string& serial, std::string& error);
};

} // namespace facescan
