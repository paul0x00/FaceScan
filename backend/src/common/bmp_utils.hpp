#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace facescan {

/// 解码后的 BMP 像素，统一按从上到下的 RGB 顺序保存。
struct BmpImageData {
    int width;
    int height;
    std::vector<unsigned char> rgb;

    BmpImageData() : width(0), height(0) {}
    bool valid() const;
};

/// 将从上到下排列的 RGB888 像素编码为 24 位 BMP。
std::vector<unsigned char> encodeBmpRgb(
    std::uint32_t width,
    std::uint32_t height,
    const std::vector<unsigned char>& rgb);

/// 将从上到下排列的 8 位灰度像素编码为带灰度调色板的 8 位 BMP。
std::vector<unsigned char> encodeBmpGray(
    std::uint32_t width,
    std::uint32_t height,
    const std::vector<unsigned char>& gray);

/// 写出 24 位 RGB BMP。
bool writeBmpRgb(
    const std::string& path,
    std::uint32_t width,
    std::uint32_t height,
    const std::vector<unsigned char>& rgb);

/// 写出带灰度调色板的 8 位 BMP。
bool writeBmpGray(
    const std::string& path,
    std::uint32_t width,
    std::uint32_t height,
    const std::vector<unsigned char>& gray);

/// 读取未压缩的 8 位索引色或 24 位 BMP，并转换为 RGB888。
bool loadBmp(const std::string& path, BmpImageData* image);

} // namespace facescan
