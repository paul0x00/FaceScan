#pragma once

#include <atomic>
#include <string>
#include <vector>

namespace facescan {

class CameraManager {
public:
    explicit CameraManager(const std::string& imageRoot);

    void start();
    void stop();
    bool streaming() const;
    void setImageRoot(const std::string& imageRoot);

    static std::string normalizeRoot(const std::string& path);

    std::string frameSvg(const std::string& view);
    std::vector<std::string> capture(const std::string& orderFolder, const std::string& orderName);

private:
    std::string imageRoot_;
    std::atomic<bool> streaming_;
    std::atomic<int> frameIndex_;

    static std::string toString(int value);
    static std::string cameraLabel(const std::string& view);
    static int colorHue(const std::string& view);
};

} // namespace facescan
