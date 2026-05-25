#include "algorithm/color_point_cloud_sdk.hpp"

#include "common/file_utils.hpp"
#include "common/time_utils.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <sstream>

namespace facescan {

namespace {

struct Rgb {
    int r;
    int g;
    int b;

    Rgb() : r(180), g(190), b(196) {}
    Rgb(int rr, int gg, int bb) : r(rr), g(gg), b(bb) {}
};

struct Point {
    double x;
    double y;
    double z;
    Rgb color;
};

struct ImageSource {
    std::string path;
    std::string view;
    int width;
    int height;
    std::vector<Rgb> pixels;
    Rgb colorA;
    Rgb colorB;

    ImageSource() : width(0), height(0), colorA(172, 205, 214), colorB(120, 164, 181) {}

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

    static double clamp(double value, double low, double high)
    {
        return std::max(low, std::min(high, value));
    }

    static Rgb mix(const Rgb& a, const Rgb& b, double t)
    {
        const double cc = clamp(t, 0.0, 1.0);
        return Rgb(
            static_cast<int>(a.r + (b.r - a.r) * cc),
            static_cast<int>(a.g + (b.g - a.g) * cc),
            static_cast<int>(a.b + (b.b - a.b) * cc));
    }
};

int clampByte(int value)
{
    return std::max(0, std::min(255, value));
}

double clampDouble(double value, double low, double high)
{
    return std::max(low, std::min(high, value));
}

std::string toString(int value)
{
    std::ostringstream os;
    os << value;
    return os.str();
}

std::string viewFromPath(const std::string& path)
{
    if (path.find("_left") != std::string::npos) return "left";
    if (path.find("_right") != std::string::npos) return "right";
    if (path.find("_bottom") != std::string::npos) return "bottom";
    if (path.find("_front") != std::string::npos) return "front";
    return "front";
}

unsigned int pathHash(const std::string& path)
{
    unsigned int hash = 2166136261u;
    for (std::size_t i = 0; i < path.size(); ++i) {
        hash ^= static_cast<unsigned char>(path[i]);
        hash *= 16777619u;
    }
    return hash;
}

Rgb fallbackColor(const std::string& path, int offset)
{
    const unsigned int h = pathHash(path) + static_cast<unsigned int>(offset * 53);
    return Rgb(
        90 + static_cast<int>(h % 116),
        95 + static_cast<int>((h >> 8) % 112),
        105 + static_cast<int>((h >> 16) % 106));
}

double hueToRgb(double p, double q, double t)
{
    if (t < 0.0) t += 1.0;
    if (t > 1.0) t -= 1.0;
    if (t < 1.0 / 6.0) return p + (q - p) * 6.0 * t;
    if (t < 1.0 / 2.0) return q;
    if (t < 2.0 / 3.0) return p + (q - p) * (2.0 / 3.0 - t) * 6.0;
    return p;
}

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

ImageSource loadImageSource(const std::string& path)
{
    ImageSource source;
    source.path = path;
    source.view = viewFromPath(path);
    source.colorA = fallbackColor(path, 0);
    source.colorB = fallbackColor(path, 1);
    if (!loadPpm(path, &source)) {
        loadSvgColors(path, &source);
    }
    return source;
}

double faceMask(double a, double b)
{
    return a * a + (b * 1.08) * (b * 1.08);
}

double faceDepth(double a, double b)
{
    const double mask = clampDouble(1.0 - faceMask(a, b), 0.0, 1.0);
    const double dome = 24.0 * std::sqrt(mask);
    const double nose = 24.0 * std::exp(-((a * a) / 0.035 + ((b + 0.03) * (b + 0.03)) / 0.055));
    const double cheeks = 8.0 * std::exp(-((std::fabs(a) - 0.42) * (std::fabs(a) - 0.42)) / 0.055 - (b * b) / 0.24);
    return 8.0 + dome + nose + cheeks;
}

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

} // namespace

ColorPointCloudSdk::ColorPointCloudSdk(const std::string& outputRoot)
    : outputRoot_(outputRoot.empty() ? "data/pointclouds" : outputRoot)
{
}

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
