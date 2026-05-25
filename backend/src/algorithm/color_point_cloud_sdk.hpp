#pragma once

#include <string>
#include <vector>

namespace facescan {

struct PointCloudOptions {
    int columns;
    int rows;
    std::string namePrefix;
    std::string outputFileName;

    PointCloudOptions() : columns(80), rows(60), namePrefix("pointcloud") {}
};

struct PointCloudBuildResult {
    bool ok;
    std::string message;
    std::string plyPath;
    int pointCount;
    std::vector<std::string> sourceImages;
    double minX;
    double minY;
    double minZ;
    double maxX;
    double maxY;
    double maxZ;

    PointCloudBuildResult()
        : ok(false),
          pointCount(0),
          minX(0.0),
          minY(0.0),
          minZ(0.0),
          maxX(0.0),
          maxY(0.0),
          maxZ(0.0)
    {
    }
};

class ColorPointCloudSdk {
public:
    explicit ColorPointCloudSdk(const std::string& outputRoot);

    PointCloudBuildResult reconstruct(
        const std::vector<std::string>& imagePaths,
        const PointCloudOptions& options = PointCloudOptions()) const;

private:
    std::string outputRoot_;
};

} // namespace facescan
