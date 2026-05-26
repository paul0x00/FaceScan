#pragma once

#include <memory>
#include <string>
#include <vector>

namespace facescan {

class ICameraDevice {
public:
    virtual ~ICameraDevice();

    virtual void start() = 0;
    virtual void stop() = 0;
    virtual bool streaming() const = 0;
    virtual void setImageRoot(const std::string& imageRoot) = 0;
    virtual std::string frameSvg(const std::string& view) = 0;
    virtual std::vector<std::string> capture(const std::string& orderFolder, const std::string& orderName) = 0;
};

class CameraManager {
public:
    explicit CameraManager(const std::string& imageRoot);
    explicit CameraManager(std::unique_ptr<ICameraDevice> device);

    void start();
    void stop();
    bool streaming() const;
    void setImageRoot(const std::string& imageRoot);

    static std::string normalizeRoot(const std::string& path);

    std::string frameSvg(const std::string& view);
    std::vector<std::string> capture(const std::string& orderFolder, const std::string& orderName);

private:
    std::unique_ptr<ICameraDevice> device_;
};

} // namespace facescan
