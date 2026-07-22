#include "point_cloud_editor.hpp"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#endif

namespace facescan {

namespace {

/// 去除 getline 在 CRLF 文件中残留的回车符。
std::string stripCarriageReturn(const std::string& value)
{
    if (!value.empty() && value[value.size() - 1] == '\r') {
        return value.substr(0, value.size() - 1);
    }
    return value;
}

/// 将临时文件替换为目标文件；同目录写入确保文件系统内替换。
bool replaceFile(const std::string& temporaryPath, const std::string& targetPath)
{
#if defined(_WIN32)
    return MoveFileExA(
               temporaryPath.c_str(),
               targetPath.c_str(),
               MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)
        != 0;
#else
    return std::rename(temporaryPath.c_str(), targetPath.c_str()) == 0;
#endif
}

} // namespace

PointCloudEditResult removePointCloudVertices(
    const std::string& path,
    const std::vector<std::size_t>& deletedIndices,
    std::size_t expectedPointCount)
{
    PointCloudEditResult result;
    std::ifstream input(path.c_str(), std::ios::binary);
    if (!input) {
        result.message = "Cannot open PLY file";
        return result;
    }

    std::vector<std::string> headerLines;
    std::string line;
    std::size_t vertexCount = 0;
    std::size_t vertexHeaderIndex = 0;
    bool asciiFormat = false;
    bool hasNonVertexElements = false;
    bool headerEnded = false;
    while (std::getline(input, line)) {
        line = stripCarriageReturn(line);
        headerLines.push_back(line);
        if (line == "format ascii 1.0") {
            asciiFormat = true;
        }
        if (line.find("element ") == 0) {
            std::istringstream parser(line);
            std::string elementKeyword;
            std::string elementName;
            std::size_t elementCount = 0;
            parser >> elementKeyword >> elementName >> elementCount;
            if (elementName == "vertex") {
                vertexCount = elementCount;
                vertexHeaderIndex = headerLines.size() - 1;
            } else if (elementCount > 0) {
                hasNonVertexElements = true;
            }
        }
        if (line == "end_header") {
            headerEnded = true;
            break;
        }
    }
    if (!headerEnded || !asciiFormat || vertexCount == 0 || hasNonVertexElements) {
        result.message = "Only vertex-only non-empty ASCII PLY point clouds are supported";
        return result;
    }
    if (expectedPointCount > 0 && vertexCount != expectedPointCount) {
        result.message = "Point cloud file has changed; reload before editing";
        return result;
    }

    std::vector<std::size_t> sortedIndices = deletedIndices;
    std::sort(sortedIndices.begin(), sortedIndices.end());
    sortedIndices.erase(std::unique(sortedIndices.begin(), sortedIndices.end()), sortedIndices.end());
    if (!sortedIndices.empty() && sortedIndices.back() >= vertexCount) {
        result.message = "Deleted point index is out of range";
        return result;
    }
    if (sortedIndices.size() >= vertexCount) {
        result.message = "At least one point must remain in the point cloud";
        return result;
    }

    std::vector<std::string> vertexLines;
    vertexLines.reserve(vertexCount);
    for (std::size_t i = 0; i < vertexCount; ++i) {
        if (!std::getline(input, line)) {
            result.message = "PLY vertex data is incomplete";
            return result;
        }
        vertexLines.push_back(stripCarriageReturn(line));
    }

    std::ostringstream trailing;
    trailing << input.rdbuf();
    input.close();

    const std::size_t remainingCount = vertexCount - sortedIndices.size();
    headerLines[vertexHeaderIndex] = "element vertex " + std::to_string(remainingCount);

    const std::string temporaryPath = path + ".editing.tmp";
    std::ofstream output(temporaryPath.c_str(), std::ios::binary | std::ios::trunc);
    if (!output) {
        result.message = "Cannot create temporary PLY file";
        return result;
    }
    for (std::size_t i = 0; i < headerLines.size(); ++i) {
        output << headerLines[i] << "\n";
    }
    std::size_t deletedPosition = 0;
    for (std::size_t i = 0; i < vertexLines.size(); ++i) {
        if (deletedPosition < sortedIndices.size() && sortedIndices[deletedPosition] == i) {
            ++deletedPosition;
            continue;
        }
        output << vertexLines[i] << "\n";
    }
    output << trailing.str();
    output.flush();
    if (!output) {
        output.close();
        std::remove(temporaryPath.c_str());
        result.message = "Cannot write edited PLY file";
        return result;
    }
    output.close();

    if (!replaceFile(temporaryPath, path)) {
        std::remove(temporaryPath.c_str());
        result.message = "Cannot replace original PLY file";
        return result;
    }

    result.ok = true;
    result.message = "Point cloud updated";
    result.originalPointCount = vertexCount;
    result.pointCount = remainingCount;
    result.removedPointCount = sortedIndices.size();
    return result;
}

} // namespace facescan
