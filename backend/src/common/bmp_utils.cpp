#include "bmp_utils.hpp"

#include "file_utils.hpp"

#include <algorithm>
#include <cstddef>
#include <fstream>
#include <limits>

namespace facescan {
namespace {

void appendByte(std::vector<unsigned char>* out, std::uint32_t value)
{
    out->push_back(static_cast<unsigned char>(value & 0xffu));
}

void appendUInt16Le(std::vector<unsigned char>* out, std::uint32_t value)
{
    appendByte(out, value);
    appendByte(out, value >> 8);
}

void appendUInt32Le(std::vector<unsigned char>* out, std::uint32_t value)
{
    appendByte(out, value);
    appendByte(out, value >> 8);
    appendByte(out, value >> 16);
    appendByte(out, value >> 24);
}

std::uint16_t readUInt16Le(const std::vector<unsigned char>& bytes, std::size_t offset)
{
    return static_cast<std::uint16_t>(bytes[offset])
        | static_cast<std::uint16_t>(bytes[offset + 1u] << 8u);
}

std::uint32_t readUInt32Le(const std::vector<unsigned char>& bytes, std::size_t offset)
{
    return static_cast<std::uint32_t>(bytes[offset])
        | (static_cast<std::uint32_t>(bytes[offset + 1u]) << 8u)
        | (static_cast<std::uint32_t>(bytes[offset + 2u]) << 16u)
        | (static_cast<std::uint32_t>(bytes[offset + 3u]) << 24u);
}

bool dimensionsValid(std::uint32_t width, std::uint32_t height, std::size_t channels, std::size_t dataSize)
{
    if (width == 0 || height == 0 || channels == 0) return false;
    const std::size_t max = std::numeric_limits<std::size_t>::max();
    if (width > max / height || static_cast<std::size_t>(width) * height > max / channels) return false;
    return dataSize >= static_cast<std::size_t>(width) * height * channels;
}

bool writeBytes(const std::string& path, const std::vector<unsigned char>& bytes)
{
    if (bytes.empty()) return false;
    ensureDirectory(parentDirectory(path));
    std::ofstream file(path.c_str(), std::ios::binary | std::ios::trunc);
    if (!file) return false;
    file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return static_cast<bool>(file);
}

std::vector<unsigned char> readBytes(const std::string& path)
{
    std::ifstream file(path.c_str(), std::ios::binary);
    if (!file) return std::vector<unsigned char>();
    file.seekg(0, std::ios::end);
    const std::streamoff size = file.tellg();
    if (size <= 0 || static_cast<std::uint64_t>(size) > std::numeric_limits<std::size_t>::max()) {
        return std::vector<unsigned char>();
    }
    file.seekg(0, std::ios::beg);
    std::vector<unsigned char> bytes(static_cast<std::size_t>(size));
    file.read(reinterpret_cast<char*>(bytes.data()), size);
    return file ? bytes : std::vector<unsigned char>();
}

} // namespace

bool BmpImageData::valid() const
{
    return width > 0 && height > 0
        && rgb.size() == static_cast<std::size_t>(width) * height * 3u;
}

std::vector<unsigned char> encodeBmpRgb(
    std::uint32_t width,
    std::uint32_t height,
    const std::vector<unsigned char>& rgb)
{
    if (!dimensionsValid(width, height, 3u, rgb.size())
        || width > (std::numeric_limits<std::uint32_t>::max() - 3u) / 3u) {
        return std::vector<unsigned char>();
    }
    const std::uint32_t rowBytes = width * 3u;
    const std::uint32_t stride = (rowBytes + 3u) & ~3u;
    if (height > (std::numeric_limits<std::uint32_t>::max() - 54u) / stride) {
        return std::vector<unsigned char>();
    }
    const std::uint32_t pixelBytes = stride * height;
    std::vector<unsigned char> out;
    out.reserve(54u + pixelBytes);
    out.push_back('B');
    out.push_back('M');
    appendUInt32Le(&out, 54u + pixelBytes);
    appendUInt16Le(&out, 0);
    appendUInt16Le(&out, 0);
    appendUInt32Le(&out, 54u);
    appendUInt32Le(&out, 40u);
    appendUInt32Le(&out, width);
    appendUInt32Le(&out, height);
    appendUInt16Le(&out, 1u);
    appendUInt16Le(&out, 24u);
    appendUInt32Le(&out, 0u);
    appendUInt32Le(&out, pixelBytes);
    appendUInt32Le(&out, 2835u);
    appendUInt32Le(&out, 2835u);
    appendUInt32Le(&out, 0u);
    appendUInt32Le(&out, 0u);
    for (std::uint32_t y = height; y > 0; --y) {
        const std::size_t row = static_cast<std::size_t>(y - 1u) * width * 3u;
        for (std::uint32_t x = 0; x < width; ++x) {
            const std::size_t pixel = row + static_cast<std::size_t>(x) * 3u;
            out.push_back(rgb[pixel + 2u]);
            out.push_back(rgb[pixel + 1u]);
            out.push_back(rgb[pixel]);
        }
        out.insert(out.end(), stride - rowBytes, 0u);
    }
    return out;
}

std::vector<unsigned char> encodeBmpGray(
    std::uint32_t width,
    std::uint32_t height,
    const std::vector<unsigned char>& gray)
{
    if (!dimensionsValid(width, height, 1u, gray.size())
        || width > std::numeric_limits<std::uint32_t>::max() - 3u) {
        return std::vector<unsigned char>();
    }
    const std::uint32_t paletteBytes = 256u * 4u;
    const std::uint32_t pixelOffset = 54u + paletteBytes;
    const std::uint32_t stride = (width + 3u) & ~3u;
    if (height > (std::numeric_limits<std::uint32_t>::max() - pixelOffset) / stride) {
        return std::vector<unsigned char>();
    }
    const std::uint32_t pixelBytes = stride * height;
    std::vector<unsigned char> out;
    out.reserve(pixelOffset + pixelBytes);
    out.push_back('B');
    out.push_back('M');
    appendUInt32Le(&out, pixelOffset + pixelBytes);
    appendUInt16Le(&out, 0u);
    appendUInt16Le(&out, 0u);
    appendUInt32Le(&out, pixelOffset);
    appendUInt32Le(&out, 40u);
    appendUInt32Le(&out, width);
    appendUInt32Le(&out, height);
    appendUInt16Le(&out, 1u);
    appendUInt16Le(&out, 8u);
    appendUInt32Le(&out, 0u);
    appendUInt32Le(&out, pixelBytes);
    appendUInt32Le(&out, 2835u);
    appendUInt32Le(&out, 2835u);
    appendUInt32Le(&out, 256u);
    appendUInt32Le(&out, 256u);
    for (std::uint32_t value = 0; value < 256u; ++value) {
        appendByte(&out, value);
        appendByte(&out, value);
        appendByte(&out, value);
        appendByte(&out, 0u);
    }
    for (std::uint32_t y = height; y > 0; --y) {
        const std::size_t row = static_cast<std::size_t>(y - 1u) * width;
        out.insert(out.end(), gray.begin() + static_cast<std::ptrdiff_t>(row),
                   gray.begin() + static_cast<std::ptrdiff_t>(row + width));
        out.insert(out.end(), stride - width, 0u);
    }
    return out;
}

bool writeBmpRgb(
    const std::string& path,
    std::uint32_t width,
    std::uint32_t height,
    const std::vector<unsigned char>& rgb)
{
    return writeBytes(path, encodeBmpRgb(width, height, rgb));
}

bool writeBmpGray(
    const std::string& path,
    std::uint32_t width,
    std::uint32_t height,
    const std::vector<unsigned char>& gray)
{
    return writeBytes(path, encodeBmpGray(width, height, gray));
}

bool loadBmp(const std::string& path, BmpImageData* image)
{
    if (!image) return false;
    const std::vector<unsigned char> bytes = readBytes(path);
    if (bytes.size() < 54u || bytes[0] != 'B' || bytes[1] != 'M') return false;
    const std::uint32_t pixelOffset = readUInt32Le(bytes, 10u);
    const std::uint32_t dibSize = readUInt32Le(bytes, 14u);
    if (dibSize < 40u || bytes.size() < 14u + dibSize) return false;
    const std::int32_t signedWidth = static_cast<std::int32_t>(readUInt32Le(bytes, 18u));
    const std::int32_t signedHeight = static_cast<std::int32_t>(readUInt32Le(bytes, 22u));
    const std::uint16_t planes = readUInt16Le(bytes, 26u);
    const std::uint16_t bits = readUInt16Le(bytes, 28u);
    const std::uint32_t compression = readUInt32Le(bytes, 30u);
    if (signedWidth <= 0 || signedHeight == 0 || planes != 1u || compression != 0u || (bits != 8u && bits != 24u)) {
        return false;
    }
    const std::uint32_t width = static_cast<std::uint32_t>(signedWidth);
    const bool topDown = signedHeight < 0;
    const std::uint32_t height = static_cast<std::uint32_t>(topDown ? -static_cast<std::int64_t>(signedHeight) : signedHeight);
    const std::uint32_t bytesPerPixel = bits == 24u ? 3u : 1u;
    if (width > (std::numeric_limits<std::uint32_t>::max() - 3u) / bytesPerPixel) {
        return false;
    }
    const std::uint32_t stride = ((width * bytesPerPixel) + 3u) & ~3u;
    if (width == 0 || height == 0 || pixelOffset > bytes.size()
        || static_cast<std::uint64_t>(stride) * height > bytes.size() - pixelOffset) {
        return false;
    }

    std::vector<unsigned char> palette;
    if (bits == 8u) {
        const std::uint32_t colorsUsed = readUInt32Le(bytes, 46u);
        const std::uint32_t paletteCount = colorsUsed == 0u ? 256u : std::min(colorsUsed, 256u);
        const std::size_t paletteOffset = 14u + dibSize;
        if (paletteOffset + static_cast<std::size_t>(paletteCount) * 4u > bytes.size()
            || paletteOffset + static_cast<std::size_t>(paletteCount) * 4u > pixelOffset) {
            return false;
        }
        palette.assign(bytes.begin() + static_cast<std::ptrdiff_t>(paletteOffset),
                       bytes.begin() + static_cast<std::ptrdiff_t>(paletteOffset + paletteCount * 4u));
    }

    BmpImageData decoded;
    decoded.width = static_cast<int>(width);
    decoded.height = static_cast<int>(height);
    decoded.rgb.resize(static_cast<std::size_t>(width) * height * 3u);
    for (std::uint32_t y = 0; y < height; ++y) {
        const std::uint32_t sourceY = topDown ? y : (height - 1u - y);
        const std::size_t sourceRow = pixelOffset + static_cast<std::size_t>(sourceY) * stride;
        const std::size_t targetRow = static_cast<std::size_t>(y) * width * 3u;
        for (std::uint32_t x = 0; x < width; ++x) {
            const std::size_t target = targetRow + static_cast<std::size_t>(x) * 3u;
            if (bits == 24u) {
                const std::size_t source = sourceRow + static_cast<std::size_t>(x) * 3u;
                decoded.rgb[target] = bytes[source + 2u];
                decoded.rgb[target + 1u] = bytes[source + 1u];
                decoded.rgb[target + 2u] = bytes[source];
            } else {
                const std::uint32_t index = bytes[sourceRow + x];
                const std::size_t palettePixel = static_cast<std::size_t>(index) * 4u;
                if (palettePixel + 2u >= palette.size()) return false;
                decoded.rgb[target] = palette[palettePixel + 2u];
                decoded.rgb[target + 1u] = palette[palettePixel + 1u];
                decoded.rgb[target + 2u] = palette[palettePixel];
            }
        }
    }
    *image = decoded;
    return true;
}

} // namespace facescan
