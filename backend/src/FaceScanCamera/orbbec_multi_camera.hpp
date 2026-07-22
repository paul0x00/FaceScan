#pragma once

#include "vendor_camera.hpp"

#include <memory>

namespace ob {
class Context;
}

namespace facescan {

class OrbbecMultiBackend : public IVendorCameraBackend {
public:
    OrbbecMultiBackend();
    ~OrbbecMultiBackend();

    const char* vendor() const { return "Orbbec"; }
    std::vector<DiscoveredCamera> discover(std::string& error);
    std::shared_ptr<IVendorCamera> open(const std::string& serial, std::string& error);

private:
    std::shared_ptr<ob::Context> context_;
};

} // namespace facescan
