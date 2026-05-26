#include "camera/camera_manager.hpp"

#include "common/file_utils.hpp"
#include "common/time_utils.hpp"

#include <atomic>
#include <fstream>
#include <sstream>
#include <utility>

namespace facescan {

namespace {

class MockCameraDevice : public ICameraDevice {
public:
    explicit MockCameraDevice(const std::string& imageRoot)
        : imageRoot_(CameraManager::normalizeRoot(imageRoot)), streaming_(false), frameIndex_(0)
    {
    }

    void start()
    {
        streaming_ = true;
    }

    void stop()
    {
        streaming_ = false;
    }

    bool streaming() const
    {
        return streaming_;
    }

    void setImageRoot(const std::string& imageRoot)
    {
        imageRoot_ = CameraManager::normalizeRoot(imageRoot);
    }

    std::string frameSvg(const std::string& view)
    {
        const int idx = ++frameIndex_;
        const std::string label = cameraLabel(view);
        const int hue = colorHue(view);
        std::ostringstream os;
        os << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"640\" height=\"400\" viewBox=\"0 0 640 400\">";
        os << "<defs><linearGradient id=\"g\" x1=\"0\" x2=\"1\"><stop offset=\"0\" stop-color=\"hsl(" << hue << ",62%,72%)\"/><stop offset=\"1\" stop-color=\"hsl(" << (hue + 42) % 360 << ",55%,58%)\"/></linearGradient></defs>";
        os << "<rect width=\"640\" height=\"400\" fill=\"url(#g)\"/>";
        os << "<g opacity=\"0.34\" stroke=\"#ffffff\" stroke-width=\"2\">";
        for (int i = 0; i < 12; ++i) {
            const int x = (idx * 7 + i * 61) % 700 - 30;
            os << "<circle cx=\"" << x << "\" cy=\"" << (55 + i * 28) << "\" r=\"" << (18 + (idx + i) % 26) << "\" fill=\"none\"/>";
        }
        os << "</g>";
        os << "<rect x=\"24\" y=\"24\" width=\"592\" height=\"352\" rx=\"10\" fill=\"none\" stroke=\"rgba(255,255,255,.68)\" stroke-width=\"3\"/>";
        os << "<text x=\"320\" y=\"192\" text-anchor=\"middle\" font-family=\"Arial, sans-serif\" font-size=\"28\" font-weight=\"700\" fill=\"#1f2933\">" << label << "</text>";
        os << "<text x=\"320\" y=\"230\" text-anchor=\"middle\" font-family=\"Arial, sans-serif\" font-size=\"16\" fill=\"#2f3d4a\">" << (streaming_ ? "实时预览" : "预览暂停") << " · Frame " << idx << "</text>";
        os << "</svg>";
        return os.str();
    }

    std::vector<std::string> capture(const std::string& orderFolder, const std::string& orderName)
    {
        ensureDirectory(orderFolder);
        const char* views[] = { "left", "front", "right", "bottom" };
        std::vector<std::string> paths;
        for (int i = 0; i < 4; ++i) {
            const std::string filePath = orderFolder + "/" + orderName + "_" + views[i] + ".svg";
            std::ofstream file(filePath.c_str(), std::ios::binary);
            file << frameSvg(views[i]);
            file.close();
            paths.push_back(filePath);
        }
        return paths;
    }

private:
    std::string imageRoot_;
    std::atomic<bool> streaming_;
    std::atomic<int> frameIndex_;

    static std::string cameraLabel(const std::string& view)
    {
        if (view == "left") return "左侧相机图像";
        if (view == "front") return "正面相机图像";
        if (view == "right") return "右侧相机图像";
        if (view == "bottom") return "下方相机图像";
        return "相机图像";
    }

    static int colorHue(const std::string& view)
    {
        if (view == "left") return 174;
        if (view == "front") return 206;
        if (view == "right") return 28;
        if (view == "bottom") return 118;
        return 190;
    }
};

} // namespace

ICameraDevice::~ICameraDevice()
{
}

CameraManager::CameraManager(const std::string& imageRoot)
    : device_(new MockCameraDevice(imageRoot))
{
}

CameraManager::CameraManager(std::unique_ptr<ICameraDevice> device)
    : device_(std::move(device))
{
}

void CameraManager::start()
{
    device_->start();
}

void CameraManager::stop()
{
    device_->stop();
}

bool CameraManager::streaming() const
{
    return device_->streaming();
}

void CameraManager::setImageRoot(const std::string& imageRoot)
{
    device_->setImageRoot(imageRoot);
}

std::string CameraManager::normalizeRoot(const std::string& path)
{
    if (path.empty()) {
        return "data/images";
    }
    if (path[path.size() - 1] == '/' || path[path.size() - 1] == '\\') {
        return path.substr(0, path.size() - 1);
    }
    return path;
}

std::string CameraManager::frameSvg(const std::string& view)
{
    return device_->frameSvg(view);
}

std::vector<std::string> CameraManager::capture(const std::string& orderFolder, const std::string& orderName)
{
    return device_->capture(orderFolder, orderName);
}

} // namespace facescan
