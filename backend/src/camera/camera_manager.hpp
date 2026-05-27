#pragma once

#include <memory>
#include <stdint.h>
#include <string>
#include <vector>

namespace facescan {

struct CameraImage {
    std::string body;
    std::string contentType;

    CameraImage() : contentType("image/svg+xml; charset=utf-8") {}
    CameraImage(const std::string& imageBody, const std::string& imageContentType)
        : body(imageBody), contentType(imageContentType)
    {
    }
};

struct CameraFrameData {
    std::string stream;
    std::string format;
    uint32_t width;
    uint32_t height;
    uint64_t frameIndex;
    std::string capturedAt;
    std::vector<unsigned char> data;

    CameraFrameData() : width(0), height(0), frameIndex(0) {}
    bool valid() const { return width > 0 && height > 0 && !data.empty(); }
};

struct SynchronizedCaptureFrames {
    CameraFrameData color;
    CameraFrameData leftIr;
    CameraFrameData rightIr;
    std::string colorPath;
    std::string leftIrPath;
    std::string rightIrPath;
    std::string manifestPath;
    std::vector<std::string> texturePaths;

    bool complete() const { return color.valid() && leftIr.valid() && rightIr.valid(); }
};

class ICameraDevice {
public:
    virtual ~ICameraDevice();

    virtual void start() = 0;
    virtual void stop() = 0;
    virtual bool streaming() const = 0;
    virtual void setImageRoot(const std::string& imageRoot) = 0;
    virtual CameraImage frameImage(const std::string& view) = 0;
    virtual std::vector<std::string> capture(const std::string& orderFolder, const std::string& orderName) = 0;
};

class CameraManager {
public:
    explicit CameraManager(const std::string& imageRoot);
    CameraManager(const std::string& imageRoot, const std::string& cameraMode);
    explicit CameraManager(std::unique_ptr<ICameraDevice> device);

    void start();
    void stop();
    bool streaming() const;
    void setImageRoot(const std::string& imageRoot);

    static std::string normalizeRoot(const std::string& path);

    CameraImage frameImage(const std::string& view);
    std::vector<std::string> capture(const std::string& orderFolder, const std::string& orderName);

private:
    std::unique_ptr<ICameraDevice> device_;
};

} // namespace facescan
