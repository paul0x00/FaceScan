#include "color_point_cloud_sdk.hpp"

#include "../common/bmp_utils.hpp"
#include "../common/file_utils.hpp"
#include "../common/time_utils.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <sstream>

namespace facescan {

namespace {

/// 简单 RGB 颜色值，供 PPM/SVG 采样和 PLY 写出复用。
struct Rgb {
    int r;
    int g;
    int b;

    Rgb() : r(180), g(190), b(196) {}
    Rgb(int rr, int gg, int bb) : r(rr), g(gg), b(bb) {}
};

/// PLY 点云中的单个彩色点。
struct Point {
    double x;
    double y;
    double z;
    Rgb color;
};

/// 从源图像抽象出的可采样纹理。
struct ImageSource {
    std::string path;
    std::string view;
    int width;
    int height;
    std::vector<Rgb> pixels;
    Rgb colorA;
    Rgb colorB;

    ImageSource() : width(0), height(0), colorA(172, 205, 214), colorB(120, 164, 181) {}

    /// 按归一化坐标采样颜色；无像素数据时退化为渐变色。
    Rgb sample(double u, double v) const
    {
        const double cu = clamp(u, 0.0, 1.0);
        const double cv = clamp(v, 0.0, 1.0);
        if (!pixels.empty() && width > 0 && height > 0) {
            const int x = static_cast<int>(cu * (width - 1));
            const int y = static_cast<int>(cv * (height - 1));
            return pixels[static_cast<std::size_t>(y * width + x)];
        }
        const double t = clamp((cu * 0.72) + (cv * 0.28), 0.0, 1.0);
        return mix(colorA, colorB, t);
    }

    /// 将浮点值限制在指定范围。
    static double clamp(double value, double low, double high)
    {
        return std::max(low, std::min(high, value));
    }

    /// 在两个颜色之间做线性插值。
    static Rgb mix(const Rgb& a, const Rgb& b, double t)
    {
        const double cc = clamp(t, 0.0, 1.0);
        return Rgb(
            static_cast<int>(a.r + (b.r - a.r) * cc),
            static_cast<int>(a.g + (b.g - a.g) * cc),
            static_cast<int>(a.b + (b.b - a.b) * cc));
    }
};

/// 黑白 IR 图像，像素归一化到 0..1 后用于双目匹配。
struct GrayImage {
    std::string path;
    int width;
    int height;
    int maxValue;
    std::vector<double> pixels;

    GrayImage() : width(0), height(0), maxValue(0) {}

    bool valid() const
    {
        return width > 0 && height > 0 && pixels.size() == static_cast<std::size_t>(width * height);
    }

    double at(int x, int y) const
    {
        const int cx = std::max(0, std::min(width - 1, x));
        const int cy = std::max(0, std::min(height - 1, y));
        return pixels[static_cast<std::size_t>(cy * width + cx)];
    }
};

/// 同步采集中用于重建的三类输入：左 IR、右 IR、彩色纹理。
struct StereoCaptureInput {
    bool ok;
    std::string colorPath;
    std::string leftIrPath;
    std::string rightIrPath;

    StereoCaptureInput() : ok(false) {}
};

/// 将整数限制在 8 位颜色通道范围。
int clampByte(int value)
{
    return std::max(0, std::min(255, value));
}

/// 将浮点数限制在指定范围。
double clampDouble(double value, double low, double high)
{
    return std::max(low, std::min(high, value));
}

/// 将整数转换为字符串。
std::string toString(int value)
{
    std::ostringstream os;
    os << value;
    return os.str();
}

/// 根据文件名中的视角后缀推断相机视角。
std::string viewFromPath(const std::string& path)
{
    if (path.find("_left") != std::string::npos) return "left";
    if (path.find("_right") != std::string::npos) return "right";
    if (path.find("_bottom") != std::string::npos) return "bottom";
    if (path.find("_front") != std::string::npos) return "front";
    return "front";
}

/// 使用 FNV-1a 对路径生成稳定哈希。
unsigned int pathHash(const std::string& path)
{
    unsigned int hash = 2166136261u;
    for (std::size_t i = 0; i < path.size(); ++i) {
        hash ^= static_cast<unsigned char>(path[i]);
        hash *= 16777619u;
    }
    return hash;
}

/// 文件内容不可解析时，用路径哈希生成稳定的兜底颜色。
Rgb fallbackColor(const std::string& path, int offset)
{
    const unsigned int h = pathHash(path) + static_cast<unsigned int>(offset * 53);
    return Rgb(
        90 + static_cast<int>(h % 116),
        95 + static_cast<int>((h >> 8) % 112),
        105 + static_cast<int>((h >> 16) % 106));
}

/// HSL 转 RGB 的单通道辅助计算。
double hueToRgb(double p, double q, double t)
{
    if (t < 0.0) t += 1.0;
    if (t > 1.0) t -= 1.0;
    if (t < 1.0 / 6.0) return p + (q - p) * 6.0 * t;
    if (t < 1.0 / 2.0) return q;
    if (t < 2.0 / 3.0) return p + (q - p) * (2.0 / 3.0 - t) * 6.0;
    return p;
}

/// 将 SVG 中的 HSL 颜色转换为 RGB。
Rgb hslToRgb(double h, double s, double l)
{
    h = std::fmod(h, 360.0) / 360.0;
    s = clampDouble(s / 100.0, 0.0, 1.0);
    l = clampDouble(l / 100.0, 0.0, 1.0);
    double r = l;
    double g = l;
    double b = l;
    if (s > 0.0) {
        const double q = l < 0.5 ? l * (1.0 + s) : l + s - l * s;
        const double p = 2.0 * l - q;
        r = hueToRgb(p, q, h + 1.0 / 3.0);
        g = hueToRgb(p, q, h);
        b = hueToRgb(p, q, h - 1.0 / 3.0);
    }
    return Rgb(clampByte(static_cast<int>(r * 255.0)),
        clampByte(static_cast<int>(g * 255.0)),
        clampByte(static_cast<int>(b * 255.0)));
}

/// 从指定位置解析 hsl(...) 片段。
bool parseHslAt(const std::string& text, std::size_t pos, Rgb* out)
{
    if (pos == std::string::npos || !out) {
        return false;
    }
    pos += 4;
    char* end = NULL;
    const double h = std::strtod(text.c_str() + pos, &end);
    if (!end || *end != ',') return false;
    const double s = std::strtod(end + 1, &end);
    if (!end || *end != '%') return false;
    while (*end && *end != ',') ++end;
    if (*end != ',') return false;
    const double l = std::strtod(end + 1, &end);
    if (!end || *end != '%') return false;
    *out = hslToRgb(h, s, l);
    return true;
}

/// 从模拟 SVG 预览中提取渐变色作为点云纹理来源。
void loadSvgColors(const std::string& path, ImageSource* source)
{
    if (!source) {
        return;
    }
    const std::string text = readFileText(path);
    Rgb first;
    Rgb second;
    const std::size_t p1 = text.find("hsl(");
    const bool hasFirst = parseHslAt(text, p1, &first);
    const std::size_t p2 = p1 == std::string::npos ? std::string::npos : text.find("hsl(", p1 + 4);
    const bool hasSecond = parseHslAt(text, p2, &second);
    if (hasFirst) {
        source->colorA = first;
    }
    if (hasSecond) {
        source->colorB = second;
    }
}

/// 读取 PPM token，跳过以 # 开头的注释行。
std::string nextToken(std::istream& in)
{
    std::string token;
    while (in >> token) {
        if (!token.empty() && token[0] == '#') {
            std::string rest;
            std::getline(in, rest);
            continue;
        }
        return token;
    }
    return "";
}

/// 加载 P3/P6 PPM 图像并填充可采样像素。
bool loadPpm(const std::string& path, ImageSource* source)
{
    if (!source) {
        return false;
    }
    std::ifstream file(path.c_str(), std::ios::binary);
    if (!file) {
        return false;
    }
    const std::string magic = nextToken(file);
    if (magic != "P3" && magic != "P6") {
        return false;
    }
    const int width = std::atoi(nextToken(file).c_str());
    const int height = std::atoi(nextToken(file).c_str());
    const int maxValue = std::atoi(nextToken(file).c_str());
    if (width <= 0 || height <= 0 || maxValue <= 0) {
        return false;
    }
    source->width = width;
    source->height = height;
    source->pixels.clear();
    source->pixels.reserve(static_cast<std::size_t>(width * height));
    if (magic == "P3") {
        for (int i = 0; i < width * height; ++i) {
            const int r = std::atoi(nextToken(file).c_str());
            const int g = std::atoi(nextToken(file).c_str());
            const int b = std::atoi(nextToken(file).c_str());
            source->pixels.push_back(Rgb(
                clampByte(r * 255 / maxValue),
                clampByte(g * 255 / maxValue),
                clampByte(b * 255 / maxValue)));
        }
    } else {
        file.get();
        for (int i = 0; i < width * height; ++i) {
            unsigned char rgb[3] = { 0, 0, 0 };
            file.read(reinterpret_cast<char*>(rgb), 3);
            if (!file) {
                source->pixels.clear();
                return false;
            }
            source->pixels.push_back(Rgb(
                clampByte(static_cast<int>(rgb[0]) * 255 / maxValue),
                clampByte(static_cast<int>(rgb[1]) * 255 / maxValue),
                clampByte(static_cast<int>(rgb[2]) * 255 / maxValue)));
        }
    }
    return true;
}

/// 加载 BMP 图像并填充可采样像素。
bool loadBmpImageSource(const std::string& path, ImageSource* source)
{
    if (!source) {
        return false;
    }
    BmpImageData image;
    if (!loadBmp(path, &image) || !image.valid()) {
        return false;
    }
    source->width = image.width;
    source->height = image.height;
    source->pixels.clear();
    source->pixels.reserve(static_cast<std::size_t>(image.width * image.height));
    for (std::size_t offset = 0; offset + 2u < image.rgb.size(); offset += 3u) {
        source->pixels.push_back(Rgb(image.rgb[offset], image.rgb[offset + 1u], image.rgb[offset + 2u]));
    }
    return source->pixels.size() == static_cast<std::size_t>(image.width * image.height);
}

/// 构造图像采样源，优先读取 BMP，并保留 PPM/SVG 历史数据兼容。
ImageSource loadImageSource(const std::string& path)
{
    ImageSource source;
    source.path = path;
    source.view = viewFromPath(path);
    source.colorA = fallbackColor(path, 0);
    source.colorB = fallbackColor(path, 1);
    if (!loadBmpImageSource(path, &source) && !loadPpm(path, &source)) {
        loadSvgColors(path, &source);
    }
    return source;
}

/// 返回文件名，不包含目录。
std::string fileNameOnly(const std::string& path)
{
    const std::size_t pos = path.find_last_of("/\\");
    return pos == std::string::npos ? path : path.substr(pos + 1);
}

/// 去掉最后一个扩展名。
std::string removeExtension(const std::string& name)
{
    const std::size_t pos = name.find_last_of('.');
    return pos == std::string::npos ? name : name.substr(0, pos);
}

/// 判断字符串是否以指定后缀结尾。
bool endsWith(const std::string& value, const std::string& suffix)
{
    return value.size() >= suffix.size()
        && value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
}

/// 去掉订单图片的视角/用途后缀，得到同一订单的文件名前缀。
std::string captureStem(const std::string& path)
{
    const char* suffixes[] = { "_left", "_front", "_right", "_bottom", "_color", "_left_ir", "_right_ir" };
    std::string stem = removeExtension(fileNameOnly(path));
    for (std::size_t suffixIndex = 0; suffixIndex < sizeof(suffixes) / sizeof(suffixes[0]); ++suffixIndex) {
        const std::string suffix = suffixes[suffixIndex];
        if (endsWith(stem, suffix)) {
            return stem.substr(0, stem.size() - suffix.size());
        }
    }
    return stem;
}

/// 尝试从指定订单根目录构造新多相机 BMP 三件套。
StereoCaptureInput findBmpStereoCaptureInput(const std::string& orderRoot, const std::string& stem)
{
    StereoCaptureInput input;
    if (orderRoot.empty() || stem.empty()) {
        return input;
    }
    input.colorPath = orderRoot + "/front/" + stem + "_color.bmp";
    input.leftIrPath = orderRoot + "/front/" + stem + "_left.bmp";
    input.rightIrPath = orderRoot + "/front/" + stem + "_right.bmp";
    input.ok = pathExists(input.colorPath) && pathExists(input.leftIrPath) && pathExists(input.rightIrPath);
    if (input.ok) {
        return input;
    }

    input.colorPath = orderRoot + "/hik_color/" + stem + "_front.bmp";
    input.leftIrPath = orderRoot + "/mindvision_left/" + stem + "_left.bmp";
    input.rightIrPath = orderRoot + "/mindvision_right/" + stem + "_right.bmp";
    input.ok = pathExists(input.colorPath) && pathExists(input.leftIrPath) && pathExists(input.rightIrPath);
    return input;
}

/// 从展示图路径推导新 BMP 目录或历史 PPM/PGM 同步采集三件套。
StereoCaptureInput findStereoCaptureInput(const std::vector<std::string>& imagePaths)
{
    for (std::size_t i = 0; i < imagePaths.size(); ++i) {
        const std::string folder = parentDirectory(imagePaths[i]);
        const std::string stem = captureStem(imagePaths[i]);
        if (folder.empty() || stem.empty()) {
            continue;
        }

        const std::string parent = parentDirectory(folder);
        StereoCaptureInput input = findBmpStereoCaptureInput(parent, stem);
        if (input.ok) {
            return input;
        }
        input = findBmpStereoCaptureInput(folder, stem);
        if (input.ok) {
            return input;
        }

        input.colorPath = folder + "/" + stem + "_color.ppm";
        input.leftIrPath = folder + "/" + stem + "_left_ir.pgm";
        input.rightIrPath = folder + "/" + stem + "_right_ir.pgm";
        input.ok = pathExists(input.colorPath) && pathExists(input.leftIrPath) && pathExists(input.rightIrPath);
        if (input.ok) {
            return input;
        }
    }
    return StereoCaptureInput();
}

/// 加载 P2/P5 PGM 图像；P5 16 位按 PGM 规范使用 big-endian。
bool loadPgm(const std::string& path, GrayImage* image)
{
    if (!image) {
        return false;
    }
    std::ifstream file(path.c_str(), std::ios::binary);
    if (!file) {
        return false;
    }
    const std::string magic = nextToken(file);
    if (magic != "P2" && magic != "P5") {
        return false;
    }
    const int width = std::atoi(nextToken(file).c_str());
    const int height = std::atoi(nextToken(file).c_str());
    const int maxValue = std::atoi(nextToken(file).c_str());
    if (width <= 0 || height <= 0 || maxValue <= 0) {
        return false;
    }
    image->path = path;
    image->width = width;
    image->height = height;
    image->maxValue = maxValue;
    image->pixels.clear();
    image->pixels.reserve(static_cast<std::size_t>(width * height));

    if (magic == "P2") {
        for (int i = 0; i < width * height; ++i) {
            const int value = std::atoi(nextToken(file).c_str());
            image->pixels.push_back(clampDouble(static_cast<double>(value) / maxValue, 0.0, 1.0));
        }
        return image->valid();
    }

    file.get();
    if (maxValue <= 255) {
        for (int i = 0; i < width * height; ++i) {
            unsigned char value = 0;
            file.read(reinterpret_cast<char*>(&value), 1);
            if (!file) {
                image->pixels.clear();
                return false;
            }
            image->pixels.push_back(static_cast<double>(value) / maxValue);
        }
        return image->valid();
    }

    for (int i = 0; i < width * height; ++i) {
        unsigned char bytes[2] = { 0, 0 };
        file.read(reinterpret_cast<char*>(bytes), 2);
        if (!file) {
            image->pixels.clear();
            return false;
        }
        const int value = (static_cast<int>(bytes[0]) << 8) | static_cast<int>(bytes[1]);
        image->pixels.push_back(clampDouble(static_cast<double>(value) / maxValue, 0.0, 1.0));
    }
    return image->valid();
}

/// 加载 BMP 并按亮度转换为双目匹配使用的灰度图。
bool loadBmpGray(const std::string& path, GrayImage* image)
{
    if (!image) {
        return false;
    }
    BmpImageData source;
    if (!loadBmp(path, &source) || !source.valid()) {
        return false;
    }
    image->path = path;
    image->width = source.width;
    image->height = source.height;
    image->maxValue = 255;
    image->pixels.clear();
    image->pixels.reserve(static_cast<std::size_t>(source.width * source.height));
    for (std::size_t offset = 0; offset + 2u < source.rgb.size(); offset += 3u) {
        const int gray = (77 * source.rgb[offset] + 150 * source.rgb[offset + 1u] + 29 * source.rgb[offset + 2u]) >> 8;
        image->pixels.push_back(static_cast<double>(gray) / 255.0);
    }
    return image->valid();
}

/// 优先读取 BMP 灰度图，并保留 PGM 历史采集兼容。
bool loadGrayImage(const std::string& path, GrayImage* image)
{
    return loadBmpGray(path, image) || loadPgm(path, image);
}

/// 椭圆面部轮廓掩膜，用于过滤正面/侧面采样点。
double faceMask(double a, double b)
{
    return a * a + (b * 1.08) * (b * 1.08);
}

/// 根据归一化面部坐标生成近似面部深度。
double faceDepth(double a, double b)
{
    const double mask = clampDouble(1.0 - faceMask(a, b), 0.0, 1.0);
    const double dome = 24.0 * std::sqrt(mask);
    const double nose = 24.0 * std::exp(-((a * a) / 0.035 + ((b + 0.03) * (b + 0.03)) / 0.055));
    const double cheeks = 8.0 * std::exp(-((std::fabs(a) - 0.42) * (std::fabs(a) - 0.42)) / 0.055 - (b * b) / 0.24);
    return 8.0 + dome + nose + cheeks;
}

/// 将指定视角的二维采样点映射到三维点云坐标。
Point makePoint(const std::string& view, double u, double v, const Rgb& color)
{
    const double a = 2.0 * u - 1.0;
    const double b = 2.0 * v - 1.0;
    Point p;
    p.color = color;
    p.x = 70.0 * a;
    p.y = -92.0 * b;
    p.z = faceDepth(a, b);
    if (view == "left") {
        const double side = 2.0 * u - 1.0;
        p.x = -62.0 + 28.0 * std::sqrt(clampDouble(1.0 - b * b, 0.0, 1.0));
        p.y = -92.0 * b;
        p.z = 2.0 + 58.0 * side;
    } else if (view == "right") {
        const double side = 2.0 * u - 1.0;
        p.x = 62.0 - 28.0 * std::sqrt(clampDouble(1.0 - b * b, 0.0, 1.0));
        p.y = -92.0 * b;
        p.z = 2.0 + 58.0 * side;
    } else if (view == "bottom") {
        const double lower = clampDouble(v, 0.0, 1.0);
        p.x = 62.0 * a;
        p.y = -28.0 - 62.0 * lower;
        p.z = -8.0 + 45.0 * (1.0 - std::fabs(a)) + 18.0 * (1.0 - lower);
    }
    return p;
}

/// 增量更新点云结果的包围盒。
void addBounds(PointCloudBuildResult* result, const Point& p)
{
    if (!result) return;
    if (result->pointCount == 0) {
        result->minX = result->maxX = p.x;
        result->minY = result->maxY = p.y;
        result->minZ = result->maxZ = p.z;
    } else {
        result->minX = std::min(result->minX, p.x);
        result->minY = std::min(result->minY, p.y);
        result->minZ = std::min(result->minZ, p.z);
        result->maxX = std::max(result->maxX, p.x);
        result->maxY = std::max(result->maxY, p.y);
        result->maxZ = std::max(result->maxZ, p.z);
    }
}

/// 将彩色点集合写为 ASCII PLY 文件。
bool writePly(const std::string& path, const std::vector<Point>& points)
{
    ensureDirectory(parentDirectory(path));
    std::ofstream file(path.c_str(), std::ios::binary);
    if (!file) {
        return false;
    }
    file << "ply\n";
    file << "format ascii 1.0\n";
    file << "comment FaceScan initial color point cloud SDK\n";
    file << "element vertex " << points.size() << "\n";
    file << "property float x\n";
    file << "property float y\n";
    file << "property float z\n";
    file << "property uchar red\n";
    file << "property uchar green\n";
    file << "property uchar blue\n";
    file << "end_header\n";
    for (std::size_t i = 0; i < points.size(); ++i) {
        file << points[i].x << " " << points[i].y << " " << points[i].z << " "
             << clampByte(points[i].color.r) << " "
             << clampByte(points[i].color.g) << " "
             << clampByte(points[i].color.b) << "\n";
    }
    return true;
}

/// 计算两个小窗口的绝对差，用于已校正双目散斑图的水平匹配。
double patchCost(const GrayImage& left, const GrayImage& right, int lx, int rx, int y, int radius)
{
    double cost = 0.0;
    int count = 0;
    for (int dy = -radius; dy <= radius; ++dy) {
        for (int dx = -radius; dx <= radius; ++dx) {
            cost += std::fabs(left.at(lx + dx, y + dy) - right.at(rx + dx, y + dy));
            ++count;
        }
    }
    return count > 0 ? cost / count : std::numeric_limits<double>::max();
}

/// 在右 IR 图中沿同一行搜索左图像素的最佳视差。
bool matchDisparity(
    const GrayImage& left,
    const GrayImage& right,
    int x,
    int y,
    int minDisparity,
    int maxDisparity,
    int radius,
    double* disparity)
{
    if (!disparity) {
        return false;
    }
    double bestCost = std::numeric_limits<double>::max();
    double secondCost = std::numeric_limits<double>::max();
    int bestDisparity = 0;
    for (int d = minDisparity; d <= maxDisparity; ++d) {
        const int rx = x - d;
        if (rx - radius < 0 || rx + radius >= right.width) {
            continue;
        }
        const double cost = patchCost(left, right, x, rx, y, radius);
        if (cost < bestCost) {
            secondCost = bestCost;
            bestCost = cost;
            bestDisparity = d;
        } else if (cost < secondCost) {
            secondCost = cost;
        }
    }
    if (bestDisparity <= 0 || bestCost > 0.22) {
        return false;
    }
    if (secondCost < std::numeric_limits<double>::max() && secondCost / std::max(bestCost, 0.0001) < 1.015) {
        return false;
    }
    *disparity = static_cast<double>(bestDisparity);
    return true;
}

/// 使用左右黑白散斑图恢复几何，再从彩色图按同一归一化坐标采样纹理。
PointCloudBuildResult reconstructFromStereoIr(
    const StereoCaptureInput& input,
    const PointCloudOptions& options,
    const std::string& outputRoot)
{
    PointCloudBuildResult result;
    result.sourceImages.push_back(input.leftIrPath);
    result.sourceImages.push_back(input.rightIrPath);
    result.sourceImages.push_back(input.colorPath);

    GrayImage left;
    GrayImage right;
    if (!loadGrayImage(input.leftIrPath, &left) || !loadGrayImage(input.rightIrPath, &right)) {
        result.message = "Cannot read stereo grayscale images";
        return result;
    }
    if (left.width != right.width || left.height != right.height) {
        result.message = "Stereo IR image sizes do not match";
        return result;
    }

    const ImageSource color = loadImageSource(input.colorPath);
    const int columns = std::max(16, std::min(240, options.columns));
    const int rows = std::max(16, std::min(240, options.rows));
    const int radius = 3;
    const int minDisparity = 2;
    const int maxDisparity = std::max(minDisparity + 1, std::min(96, left.width / 4));
    const int x0 = maxDisparity + radius + 1;
    const int x1 = left.width - radius - 2;
    const int y0 = radius + 1;
    const int y1 = left.height - radius - 2;
    if (x1 <= x0 || y1 <= y0) {
        result.message = "Stereo IR images are too small";
        return result;
    }

    const double focalPx = std::max(left.width, left.height) * 0.90;
    const double baselineMm = 55.0;
    const double cx = (left.width - 1) * 0.5;
    const double cy = (left.height - 1) * 0.5;
    std::vector<Point> points;
    points.reserve(static_cast<std::size_t>(columns * rows));

    for (int row = 0; row < rows; ++row) {
        const double tv = rows == 1 ? 0.0 : static_cast<double>(row) / static_cast<double>(rows - 1);
        const int y = static_cast<int>(y0 + tv * (y1 - y0));
        for (int col = 0; col < columns; ++col) {
            const double tu = columns == 1 ? 0.0 : static_cast<double>(col) / static_cast<double>(columns - 1);
            const int x = static_cast<int>(x0 + tu * (x1 - x0));
            double disparity = 0.0;
            if (!matchDisparity(left, right, x, y, minDisparity, maxDisparity, radius, &disparity)) {
                continue;
            }

            const double z = clampDouble((baselineMm * focalPx) / disparity, 180.0, 1800.0);
            const double u = static_cast<double>(x) / static_cast<double>(left.width - 1);
            const double v = static_cast<double>(y) / static_cast<double>(left.height - 1);
            Point p;
            p.x = (static_cast<double>(x) - cx) * z / focalPx;
            p.y = -(static_cast<double>(y) - cy) * z / focalPx;
            p.z = z;
            p.color = color.sample(u, v);
            points.push_back(p);
            addBounds(&result, p);
            ++result.pointCount;
        }
    }

    if (points.empty()) {
        result.message = "Stereo IR reconstruction produced no points";
        return result;
    }

    const std::string prefix = options.namePrefix.empty() ? "pointcloud" : options.namePrefix;
    const std::string plyFileName = options.outputFileName.empty() ? (prefix + "_" + stampTextMs() + ".ply") : options.outputFileName;
    const std::string plyPath = outputRoot + "/" + plyFileName;
    if (!writePly(plyPath, points)) {
        result.message = "Cannot write PLY file";
        result.pointCount = 0;
        return result;
    }

    result.ok = true;
    result.message = "ok";
    result.plyPath = plyPath;
    return result;
}

} // namespace

/// 指定输出根目录，空路径时使用默认点云目录。
ColorPointCloudSdk::ColorPointCloudSdk(const std::string& outputRoot)
    : outputRoot_(outputRoot.empty() ? "data/pointclouds" : outputRoot)
{
}

/// 优先用同步采集的左右 IR 散斑图重建几何，并用彩色图贴纹理；没有硬件帧时回退到 mock 采样。
PointCloudBuildResult ColorPointCloudSdk::reconstruct(
    const std::vector<std::string>& imagePaths,
    const PointCloudOptions& options) const
{
    PointCloudBuildResult result;
    result.sourceImages = imagePaths;
    if (imagePaths.empty()) {
        result.message = "No source images";
        return result;
    }

    const StereoCaptureInput stereoInput = findStereoCaptureInput(imagePaths);
    if (stereoInput.ok) {
        return reconstructFromStereoIr(stereoInput, options, outputRoot_);
    }

    const int columns = std::max(16, std::min(240, options.columns));
    const int rows = std::max(16, std::min(240, options.rows));
    std::vector<Point> points;
    points.reserve(static_cast<std::size_t>(imagePaths.size() * columns * rows));

    for (std::size_t imageIndex = 0; imageIndex < imagePaths.size(); ++imageIndex) {
        const ImageSource source = loadImageSource(imagePaths[imageIndex]);
        for (int y = 0; y < rows; ++y) {
            const double v = rows == 1 ? 0.0 : static_cast<double>(y) / static_cast<double>(rows - 1);
            for (int x = 0; x < columns; ++x) {
                const double u = columns == 1 ? 0.0 : static_cast<double>(x) / static_cast<double>(columns - 1);
                const double a = 2.0 * u - 1.0;
                const double b = 2.0 * v - 1.0;
                if (source.view != "bottom" && faceMask(a, b) > 1.0) {
                    continue;
                }
                points.push_back(makePoint(source.view, u, v, source.sample(u, v)));
                addBounds(&result, points.back());
                ++result.pointCount;
            }
        }
    }

    const std::string prefix = options.namePrefix.empty() ? "pointcloud" : options.namePrefix;
    const std::string plyFileName = options.outputFileName.empty() ? (prefix + "_" + stampTextMs() + ".ply") : options.outputFileName;
    const std::string plyPath = outputRoot_ + "/" + plyFileName;
    if (!writePly(plyPath, points)) {
        result.message = "Cannot write PLY file";
        result.pointCount = 0;
        return result;
    }

    result.ok = true;
    result.message = "ok";
    result.plyPath = plyPath;
    return result;
}

} // namespace facescan
