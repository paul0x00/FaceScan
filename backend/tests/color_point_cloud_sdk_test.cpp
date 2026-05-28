#include "algorithm/color_point_cloud_sdk.hpp"

#include "common/file_utils.hpp"
#include "tests/test_utils.hpp"

#include <fstream>

#include <gtest/gtest.h>

using namespace facescan;

namespace {

unsigned char speckleValue(int x, int y)
{
    unsigned int value = static_cast<unsigned int>(x * 73856093u) ^ static_cast<unsigned int>(y * 19349663u);
    value ^= value >> 13;
    return static_cast<unsigned char>(value & 0xffu);
}

void writePgm(const std::string& path, int width, int height, int shift)
{
    std::ofstream file(path.c_str(), std::ios::binary | std::ios::trunc);
    file << "P5\n" << width << " " << height << "\n255\n";
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const int sourceX = x + shift;
            const unsigned char value = sourceX >= 0 && sourceX < width ? speckleValue(sourceX, y) : 0;
            file.write(reinterpret_cast<const char*>(&value), 1);
        }
    }
}

void writePpm(const std::string& path, int width, int height)
{
    std::ofstream file(path.c_str(), std::ios::binary | std::ios::trunc);
    file << "P6\n" << width << " " << height << "\n255\n";
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const unsigned char rgb[3] = {
                static_cast<unsigned char>(x * 255 / width),
                static_cast<unsigned char>(y * 255 / height),
                static_cast<unsigned char>(180),
            };
            file.write(reinterpret_cast<const char*>(rgb), 3);
        }
    }
}

} // namespace

TEST(ColorPointCloudSdkTest, ReconstructsGeometryFromStereoIrAndUsesColorTexture)
{
    facescan_test::ScopedTempDir temp("stereo_ir_reconstruct");
    const std::string base = temp.path() + "/ORDER001";
    writePpm(base + "_color.ppm", 160, 120);
    writePgm(base + "_left_ir.pgm", 160, 120, 0);
    writePgm(base + "_right_ir.pgm", 160, 120, 8);

    PointCloudOptions options;
    options.columns = 48;
    options.rows = 36;
    options.namePrefix = "ORDER001";
    options.outputFileName = "ORDER001.ply";

    ColorPointCloudSdk sdk(temp.path());
    const PointCloudBuildResult result = sdk.reconstruct(std::vector<std::string>(1, base + "_left.ppm"), options);

    EXPECT_TRUE(result.ok) << result.message;
    EXPECT_GT(result.pointCount, 100);
    ASSERT_EQ(3u, result.sourceImages.size());
    EXPECT_NE(std::string::npos, result.sourceImages[0].find("_left_ir.pgm"));
    EXPECT_NE(std::string::npos, result.sourceImages[1].find("_right_ir.pgm"));
    EXPECT_NE(std::string::npos, result.sourceImages[2].find("_color.ppm"));
    EXPECT_TRUE(pathExists(result.plyPath));
    EXPECT_NE(std::string::npos, readFileText(result.plyPath).find("property uchar red"));
}
