#pragma once

#include "../common/module_api.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace facescan {

/// ASCII PLY 点云编辑结果。
struct PointCloudEditResult {
    bool ok;
    std::string message;
    std::size_t originalPointCount;
    std::size_t pointCount;
    std::size_t removedPointCount;

    PointCloudEditResult()
        : ok(false), originalPointCount(0), pointCount(0), removedPointCount(0)
    {
    }
};

/// 按原始顶点索引删除 ASCII PLY 中的点，并通过临时文件替换源文件。
FACESCAN_RECONSTRUCTION_API PointCloudEditResult removePointCloudVertices(
    const std::string& path,
    const std::vector<std::size_t>& deletedIndices,
    std::size_t expectedPointCount = 0);

} // namespace facescan
