#pragma once

#include "../common/module_api.hpp"

#include <string>
#include <vector>

namespace facescan {

/// 彩色点云重建参数。
struct PointCloudOptions {
    /// 每张图像横向采样列数。
    int columns;
    /// 每张图像纵向采样行数。
    int rows;
    /// 自动生成 PLY 文件名时使用的前缀。
    std::string namePrefix;
    /// 指定输出文件名；为空时按前缀和时间戳生成。
    std::string outputFileName;

    /// 构造 MVP 默认采样参数。
    PointCloudOptions() : columns(80), rows(60), namePrefix("pointcloud") {}
};

/// 点云重建输出结果。
struct PointCloudBuildResult {
    /// 重建是否成功。
    bool ok;
    /// 成功或失败说明。
    std::string message;
    /// 写出的 PLY 文件路径。
    std::string plyPath;
    /// 输出点数量。
    int pointCount;
    /// 本次重建使用的源图像路径。
    std::vector<std::string> sourceImages;
    /// 点云包围盒最小 X。
    double minX;
    /// 点云包围盒最小 Y。
    double minY;
    /// 点云包围盒最小 Z。
    double minZ;
    /// 点云包围盒最大 X。
    double maxX;
    /// 点云包围盒最大 Y。
    double maxY;
    /// 点云包围盒最大 Z。
    double maxZ;

    /// 构造失败态空结果。
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

/// MVP 彩色点云重建器，将多视角图像转换为 PLY 点云。
class FACESCAN_RECONSTRUCTION_API ColorPointCloudSdk {
public:
    /// 指定点云输出目录。
    explicit ColorPointCloudSdk(const std::string& outputRoot);

    /// 根据采集图像生成彩色 PLY 点云。
    PointCloudBuildResult reconstruct(
        const std::vector<std::string>& imagePaths,
        const PointCloudOptions& options = PointCloudOptions()) const;

private:
    /// PLY 文件输出根目录。
    std::string outputRoot_;
};

} // namespace facescan
