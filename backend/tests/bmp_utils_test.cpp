#include "../src/common/bmp_utils.hpp"

#include "../src/common/file_utils.hpp"
#include "test_utils.hpp"

#include <cstdint>
#include <vector>

#include <gtest/gtest.h>

using namespace facescan;

namespace {

std::uint32_t readUInt32Le(const std::vector<unsigned char>& bytes, std::size_t offset)
{
    return static_cast<std::uint32_t>(bytes[offset])
        | (static_cast<std::uint32_t>(bytes[offset + 1u]) << 8u)
        | (static_cast<std::uint32_t>(bytes[offset + 2u]) << 16u)
        | (static_cast<std::uint32_t>(bytes[offset + 3u]) << 24u);
}

} // namespace

TEST(BmpUtilsTest, WritesAndReadsRgbBmpWithoutChangingPixels)
{
    facescan_test::ScopedTempDir temp("bmp_rgb");
    const std::string path = temp.path() + "/rgb.bmp";
    const std::vector<unsigned char> pixels = {
        255, 0, 0, 0, 255, 0, 0, 0, 255,
        12, 34, 56, 78, 90, 123, 210, 180, 140,
    };

    ASSERT_TRUE(writeBmpRgb(path, 3, 2, pixels));
    const std::string bytes = readFileText(path);
    ASSERT_GE(bytes.size(), 54u);
    EXPECT_EQ('B', bytes[0]);
    EXPECT_EQ('M', bytes[1]);
    EXPECT_EQ(24, static_cast<unsigned char>(bytes[28]));

    BmpImageData decoded;
    ASSERT_TRUE(loadBmp(path, &decoded));
    EXPECT_EQ(3, decoded.width);
    EXPECT_EQ(2, decoded.height);
    EXPECT_EQ(pixels, decoded.rgb);
}

TEST(BmpUtilsTest, WritesIndexedGrayBmpWithPaletteAndReadsItAsRgb)
{
    facescan_test::ScopedTempDir temp("bmp_gray");
    const std::string path = temp.path() + "/gray.bmp";
    const std::vector<unsigned char> pixels = { 0, 64, 128, 255, 200, 100 };

    ASSERT_TRUE(writeBmpGray(path, 3, 2, pixels));
    const std::string body = readFileText(path);
    const std::vector<unsigned char> bytes(body.begin(), body.end());
    ASSERT_GE(bytes.size(), 1078u);
    EXPECT_EQ('B', bytes[0]);
    EXPECT_EQ('M', bytes[1]);
    EXPECT_EQ(8, bytes[28]);
    EXPECT_EQ(1078u, readUInt32Le(bytes, 10u));
    EXPECT_EQ(0, bytes[54]);
    EXPECT_EQ(1, bytes[58]);
    EXPECT_EQ(255, bytes[54u + 255u * 4u]);

    BmpImageData decoded;
    ASSERT_TRUE(loadBmp(path, &decoded));
    ASSERT_EQ(pixels.size() * 3u, decoded.rgb.size());
    for (std::size_t i = 0; i < pixels.size(); ++i) {
        EXPECT_EQ(pixels[i], decoded.rgb[i * 3u]);
        EXPECT_EQ(pixels[i], decoded.rgb[i * 3u + 1u]);
        EXPECT_EQ(pixels[i], decoded.rgb[i * 3u + 2u]);
    }
}
