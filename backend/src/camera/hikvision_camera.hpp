#pragma once

#include "vendor_camera.hpp"

namespace facescan {

/// 海康威视 MVS SDK 后端。
class HikvisionBackend : public IVendorCameraBackend {
public:
    HikvisionBackend();
    ~HikvisionBackend();

    const char* vendor() const { return "Hikvision"; }
    std::vector<DiscoveredCamera> discover(std::string& error);
    std::shared_ptr<IVendorCamera> open(const std::string& serial, std::string& error);
};

} // namespace facescan
