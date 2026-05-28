#include "camera_manager.hpp"

#include "../common/file_utils.hpp"
#include "../common/json_utils.hpp"
#include "../common/time_utils.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <fstream>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <utility>

#if defined(FACESCAN_WITH_ORBBEC)
#include <libobsensor/ObSensor.hpp>
#include <libobsensor/hpp/TypeHelper.hpp>
#endif

namespace facescan {

namespace {

/// 前端固定展示的四个采集视角。
const char* kViewNames[] = { "left", "front", "right", "bottom" };

/// 向字符串追加一个低 8 位字节。
void appendByte(std::string* out, unsigned int value)
{
    out->push_back(static_cast<char>(value & 0xffu));
}

/// 按小端序追加 16 位整数。
void appendUInt16Le(std::string* out, unsigned int value)
{
    appendByte(out, value);
    appendByte(out, value >> 8);
}

/// 按小端序追加 32 位整数。
void appendUInt32Le(std::string* out, unsigned int value)
{
    appendByte(out, value);
    appendByte(out, value >> 8);
    appendByte(out, value >> 16);
    appendByte(out, value >> 24);
}

/// 将 RGB 帧编码为无需额外依赖的 BMP 预览图。
std::string bmpFromRgb(const CameraFrameData& frame)
{
    if (!frame.valid() || frame.data.size() < static_cast<std::size_t>(frame.width) * frame.height * 3u) {
        return "";
    }
    const unsigned int rowBytes = frame.width * 3u;
    const unsigned int padding = (4u - (rowBytes % 4u)) % 4u;
    const unsigned int pixelBytes = (rowBytes + padding) * frame.height;
    const unsigned int fileBytes = 54u + pixelBytes;

    std::string out;
    out.reserve(fileBytes);
    out.push_back('B');
    out.push_back('M');
    appendUInt32Le(&out, fileBytes);
    appendUInt16Le(&out, 0);
    appendUInt16Le(&out, 0);
    appendUInt32Le(&out, 54);
    appendUInt32Le(&out, 40);
    appendUInt32Le(&out, frame.width);
    appendUInt32Le(&out, frame.height);
    appendUInt16Le(&out, 1);
    appendUInt16Le(&out, 24);
    appendUInt32Le(&out, 0);
    appendUInt32Le(&out, pixelBytes);
    appendUInt32Le(&out, 2835);
    appendUInt32Le(&out, 2835);
    appendUInt32Le(&out, 0);
    appendUInt32Le(&out, 0);

    for (int y = static_cast<int>(frame.height) - 1; y >= 0; --y) {
        const std::size_t row = static_cast<std::size_t>(y) * frame.width * 3u;
        for (uint32_t x = 0; x < frame.width; ++x) {
            const std::size_t p = row + static_cast<std::size_t>(x) * 3u;
            out.push_back(static_cast<char>(frame.data[p + 2]));
            out.push_back(static_cast<char>(frame.data[p + 1]));
            out.push_back(static_cast<char>(frame.data[p]));
        }
        for (unsigned int i = 0; i < padding; ++i) {
            out.push_back('\0');
        }
    }
    return out;
}

/// 将 RGB 彩色帧写为 PPM 文件。
void writePpm(const std::string& path, const CameraFrameData& frame)
{
    if (!frame.valid() || frame.data.size() < static_cast<std::size_t>(frame.width) * frame.height * 3u) {
        throw std::runtime_error("Invalid RGB color frame");
    }
    ensureDirectory(parentDirectory(path));
    std::ofstream file(path.c_str(), std::ios::binary | std::ios::trunc);
    if (!file) {
        throw std::runtime_error("Cannot write color frame: " + path);
    }
    file << "P6\n" << frame.width << " " << frame.height << "\n255\n";
    file.write(reinterpret_cast<const char*>(&frame.data[0]), static_cast<std::streamsize>(frame.width * frame.height * 3u));
}

/// 将红外帧写为 PGM 文件，兼容 8 位和 16 位灰度数据。
void writePgm(const std::string& path, const CameraFrameData& frame)
{
    if (!frame.valid()) {
        throw std::runtime_error("Invalid IR frame");
    }
    const std::size_t pixelCount = static_cast<std::size_t>(frame.width) * frame.height;
    ensureDirectory(parentDirectory(path));
    std::ofstream file(path.c_str(), std::ios::binary | std::ios::trunc);
    if (!file) {
        throw std::runtime_error("Cannot write IR frame: " + path);
    }
    if (frame.data.size() == pixelCount || frame.format == "Y8") {
        file << "P5\n" << frame.width << " " << frame.height << "\n255\n";
        file.write(reinterpret_cast<const char*>(&frame.data[0]), static_cast<std::streamsize>(std::min(frame.data.size(), pixelCount)));
        return;
    }
    if (frame.data.size() >= pixelCount * 2u) {
        file << "P5\n" << frame.width << " " << frame.height << "\n65535\n";
        for (std::size_t i = 0; i < pixelCount; ++i) {
            uint16_t value = 0;
            std::memcpy(&value, &frame.data[i * 2u], sizeof(value));
            file.put(static_cast<char>((value >> 8) & 0xffu));
            file.put(static_cast<char>(value & 0xffu));
        }
        return;
    }
    throw std::runtime_error("Unsupported IR frame layout: " + frame.format);
}

/// 写入一次同步采集的元数据清单，便于排查真实设备采集问题。
void writeCaptureManifest(const SynchronizedCaptureFrames& capture)
{
    if (capture.manifestPath.empty()) {
        return;
    }
    ensureDirectory(parentDirectory(capture.manifestPath));
    std::ofstream file(capture.manifestPath.c_str(), std::ios::binary | std::ios::trunc);
    if (!file) {
        throw std::runtime_error("Cannot write capture manifest: " + capture.manifestPath);
    }
    file << "{\n";
    file << "  \"complete\": " << (capture.complete() ? "true" : "false") << ",\n";
    file << "  \"colorPath\": \"" << escapeJson(capture.colorPath) << "\",\n";
    file << "  \"leftIrPath\": \"" << escapeJson(capture.leftIrPath) << "\",\n";
    file << "  \"rightIrPath\": \"" << escapeJson(capture.rightIrPath) << "\",\n";
    file << "  \"frames\": {\n";
    file << "    \"color\": {\"width\": " << capture.color.width << ", \"height\": " << capture.color.height
         << ", \"format\": \"" << escapeJson(capture.color.format) << "\", \"frameIndex\": " << capture.color.frameIndex << "},\n";
    file << "    \"leftIr\": {\"width\": " << capture.leftIr.width << ", \"height\": " << capture.leftIr.height
         << ", \"format\": \"" << escapeJson(capture.leftIr.format) << "\", \"frameIndex\": " << capture.leftIr.frameIndex << "},\n";
    file << "    \"rightIr\": {\"width\": " << capture.rightIr.width << ", \"height\": " << capture.rightIr.height
         << ", \"format\": \"" << escapeJson(capture.rightIr.format) << "\", \"frameIndex\": " << capture.rightIr.frameIndex << "}\n";
    file << "  }\n";
    file << "}\n";
}

/// 模拟相机实现，用 SVG 生成稳定的四路预览和采集图片。
class MockCameraDevice : public ICameraDevice {
public:
    /// 使用指定图片根目录初始化模拟相机。
    explicit MockCameraDevice(const std::string& imageRoot)
        : imageRoot_(CameraManager::normalizeRoot(imageRoot)), streaming_(false), frameIndex_(0)
    {
    }

    /// 标记模拟预览为运行状态。
    void start()
    {
        streaming_ = true;
    }

    /// 标记模拟预览为停止状态。
    void stop()
    {
        streaming_ = false;
    }

    /// 返回模拟预览状态。
    bool streaming() const
    {
        return streaming_;
    }

    /// 更新模拟采集保存根目录。
    void setImageRoot(const std::string& imageRoot)
    {
        imageRoot_ = CameraManager::normalizeRoot(imageRoot);
    }

    /// 生成指定视角的 SVG 预览图。
    CameraImage frameImage(const std::string& view)
    {
        return CameraImage(frameSvg(view), "image/svg+xml; charset=utf-8");
    }

    /// 写出四张 SVG 采集图并返回文件路径。
    std::vector<std::string> capture(const std::string& orderFolder, const std::string& orderName)
    {
        ensureDirectory(orderFolder);
        std::vector<std::string> paths;
        for (int i = 0; i < 4; ++i) {
            const std::string filePath = orderFolder + "/" + orderName + "_" + kViewNames[i] + ".svg";
            std::ofstream file(filePath.c_str(), std::ios::binary);
            file << frameSvg(kViewNames[i]);
            file.close();
            paths.push_back(filePath);
        }
        return paths;
    }

private:
    /// 当前图片保存根目录。
    std::string imageRoot_;
    /// 模拟预览是否运行。
    std::atomic<bool> streaming_;
    /// 用于制造动态预览效果的帧序号。
    std::atomic<int> frameIndex_;

    /// 根据视角和帧序号绘制模拟相机 SVG。
    std::string frameSvg(const std::string& view)
    {
        const int idx = ++frameIndex_;
        const std::string label = cameraLabel(view);
        const int hue = colorHue(view);
        std::ostringstream os;
        os << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"640\" height=\"400\" viewBox=\"0 0 640 400\">";
        os << "<defs><linearGradient id=\"g\" x1=\"0\" x2=\"1\"><stop offset=\"0\" stop-color=\"hsl(" << hue << ",62%,72%)\"/><stop offset=\"1\" stop-color=\"hsl(" << (hue + 42) % 360 << ",55%,58%)\"/></linearGradient></defs>";
        os << "<rect width=\"640\" height=\"400\" fill=\"url(#g)\"/>";
        os << "<g opacity=\"0.34\" stroke=\"#ffffff\" stroke-width=\"2\">";
        for (int i = 0; i < 12; ++i) {
            const int x = (idx * 7 + i * 61) % 700 - 30;
            os << "<circle cx=\"" << x << "\" cy=\"" << (55 + i * 28) << "\" r=\"" << (18 + (idx + i) % 26) << "\" fill=\"none\"/>";
        }
        os << "</g>";
        os << "<rect x=\"24\" y=\"24\" width=\"592\" height=\"352\" rx=\"10\" fill=\"none\" stroke=\"rgba(255,255,255,.68)\" stroke-width=\"3\"/>";
        os << "<text x=\"320\" y=\"192\" text-anchor=\"middle\" font-family=\"Arial, sans-serif\" font-size=\"28\" font-weight=\"700\" fill=\"#1f2933\">" << label << "</text>";
        os << "<text x=\"320\" y=\"230\" text-anchor=\"middle\" font-family=\"Arial, sans-serif\" font-size=\"16\" fill=\"#2f3d4a\">" << (streaming_ ? "实时预览" : "预览暂停") << " · Frame " << idx << "</text>";
        os << "</svg>";
        return os.str();
    }

    /// 返回视角的中文展示名。
    static std::string cameraLabel(const std::string& view)
    {
        if (view == "left") return "左侧相机图像";
        if (view == "front") return "正面相机图像";
        if (view == "right") return "右侧相机图像";
        if (view == "bottom") return "下方相机图像";
        return "相机图像";
    }

    /// 返回视角对应的基础色相。
    static int colorHue(const std::string& view)
    {
        if (view == "left") return 174;
        if (view == "front") return 206;
        if (view == "right") return 28;
        if (view == "bottom") return 118;
        return 190;
    }
};

#if defined(FACESCAN_WITH_ORBBEC)

/// 将 Orbbec SDK 图像格式转换为可读文本。
std::string formatName(OBFormat format)
{
    return ob::TypeHelper::convertOBFormatTypeToString(format);
}

/// 从帧集合中查找可用的红外帧，兼容不同设备的帧类型命名。
std::shared_ptr<ob::IRFrame> getIrFrameFromSet(const std::shared_ptr<ob::FrameSet>& frameSet)
{
    if (!frameSet) {
        return std::shared_ptr<ob::IRFrame>();
    }
    const OBFrameType candidates[] = { OB_FRAME_IR, OB_FRAME_IR_LEFT, OB_FRAME_IR_RIGHT };
    for (int i = 0; i < 3; ++i) {
        std::shared_ptr<ob::Frame> frame = frameSet->getFrame(candidates[i]);
        if (frame) {
            return frame->as<ob::IRFrame>();
        }
    }
    for (uint32_t i = 0; i < frameSet->getCount(); ++i) {
        std::shared_ptr<ob::Frame> frame = frameSet->getFrameByIndex(i);
        if (!frame) {
            continue;
        }
        const OBFrameType type = frame->getType();
        if (type == OB_FRAME_IR || type == OB_FRAME_IR_LEFT || type == OB_FRAME_IR_RIGHT) {
            return frame->as<ob::IRFrame>();
        }
    }
    return std::shared_ptr<ob::IRFrame>();
}

/// 创建彩色流配置。
std::shared_ptr<ob::Config> makeColorConfig()
{
    std::shared_ptr<ob::Config> config(new ob::Config());
    config->enableVideoStream(OB_SENSOR_COLOR, OB_WIDTH_ANY, OB_HEIGHT_ANY, OB_FPS_ANY, OB_FORMAT_RGB);
    return config;
}

/// 创建红外流配置。
std::shared_ptr<ob::Config> makeIrConfig()
{
    std::shared_ptr<ob::Config> config(new ob::Config());
    config->enableVideoStream(OB_SENSOR_IR, OB_WIDTH_ANY, OB_HEIGHT_ANY, OB_FPS_ANY, OB_FORMAT_ANY);
    return config;
}

/// 复制 Orbbec 彩色帧到项目内部帧结构。
CameraFrameData copyColorFrame(const std::shared_ptr<ob::ColorFrame>& frame)
{
    CameraFrameData out;
    if (!frame) {
        return out;
    }
    out.stream = "color";
    out.format = formatName(frame->getFormat());
    out.width = frame->getWidth();
    out.height = frame->getHeight();
    out.frameIndex = frame->getIndex();
    out.capturedAt = nowText();
    const uint8_t* data = frame->getData();
    const uint32_t dataSize = frame->getDataSize();
    if (data && dataSize > 0) {
        out.data.assign(data, data + dataSize);
    }
    return out;
}

/// 复制 Orbbec 红外帧到项目内部帧结构。
CameraFrameData copyIrFrame(const std::shared_ptr<ob::IRFrame>& frame, const std::string& stream)
{
    CameraFrameData out;
    if (!frame) {
        return out;
    }
    out.stream = stream;
    out.format = formatName(frame->getFormat());
    out.width = frame->getWidth();
    out.height = frame->getHeight();
    out.frameIndex = frame->getIndex();
    out.capturedAt = nowText();
    const uint8_t* data = frame->getData();
    const uint32_t dataSize = frame->getDataSize();
    if (data && dataSize > 0) {
        out.data.assign(data, data + dataSize);
    }
    return out;
}

/// Orbbec 真实相机实现，负责彩色预览和双红外同步采集。
class OrbbecCameraDevice : public ICameraDevice {
public:
    /// 使用指定图片根目录初始化真实设备。
    explicit OrbbecCameraDevice(const std::string& imageRoot)
        : imageRoot_(CameraManager::normalizeRoot(imageRoot)),
          previewRunning_(false),
          stopPreview_(false),
          latestPreviewFrameIndex_(0)
    {
    }

    /// 析构时确保预览线程和 SDK 管线停止。
    ~OrbbecCameraDevice()
    {
        try {
            stop();
        } catch (...) {
        }
    }

    /// 开启彩色预览流。
    void start()
    {
        std::lock_guard<std::mutex> lock(controlMutex_);
        startColorPreviewLocked();
    }

    /// 停止彩色预览流。
    void stop()
    {
        std::lock_guard<std::mutex> lock(controlMutex_);
        stopColorPreviewLocked();
    }

    /// 返回彩色预览是否运行。
    bool streaming() const
    {
        return previewRunning_;
    }

    /// 更新数据保存根目录。
    void setImageRoot(const std::string& imageRoot)
    {
        std::lock_guard<std::mutex> lock(dataMutex_);
        imageRoot_ = CameraManager::normalizeRoot(imageRoot);
    }

    /// 返回最近彩色帧的 BMP 预览；尚无帧时返回等待 SVG。
    CameraImage frameImage(const std::string&)
    {
        CameraFrameData frame;
        {
            std::lock_guard<std::mutex> lock(dataMutex_);
            frame = latestColor_;
            if (latestPreviewFrameIndex_ == frame.frameIndex && !latestPreview_.body.empty()) {
                return latestPreview_;
            }
        }
        const std::string bmp = bmpFromRgb(frame);
        if (!bmp.empty()) {
            CameraImage image(bmp, "image/bmp");
            std::lock_guard<std::mutex> lock(dataMutex_);
            if (latestColor_.frameIndex == frame.frameIndex) {
                latestPreview_ = image;
                latestPreviewFrameIndex_ = frame.frameIndex;
            }
            return image;
        }
        return CameraImage(waitingSvg(), "image/svg+xml; charset=utf-8");
    }

    /// 暂停预览后采集彩色、左红外和右红外帧，并写出纹理图。
    std::vector<std::string> capture(const std::string& orderFolder, const std::string& orderName)
    {
        std::lock_guard<std::mutex> lock(controlMutex_);
        const bool resumePreview = previewRunning_;
        CameraFrameData color = latestColorFrameLocked();
        if (!color.valid()) {
            if (!previewRunning_) {
                startColorPreviewLocked();
            }
            color = waitForLatestColorLocked(2000);
        }
        if (!color.valid()) {
            throw std::runtime_error("No color frame available from Orbbec camera");
        }

        stopColorPreviewLocked();

        SynchronizedCaptureFrames capture;
        capture.color = color;
        capture.colorPath = orderFolder + "/" + orderName + "_color.ppm";
        capture.leftIrPath = orderFolder + "/" + orderName + "_left_ir.pgm";
        capture.rightIrPath = orderFolder + "/" + orderName + "_right_ir.pgm";
        capture.manifestPath = orderFolder + "/" + orderName + "_sync_capture.json";

        try {
            capture.leftIr = captureIrFrameLocked(0, "leftIr");
            capture.rightIr = captureIrFrameLocked(1, "rightIr");

            ensureDirectory(orderFolder);
            writePpm(capture.colorPath, capture.color);
            writePgm(capture.leftIrPath, capture.leftIr);
            writePgm(capture.rightIrPath, capture.rightIr);

            for (int i = 0; i < 4; ++i) {
                const std::string texturePath = orderFolder + "/" + orderName + "_" + kViewNames[i] + ".ppm";
                writePpm(texturePath, capture.color);
                capture.texturePaths.push_back(texturePath);
            }
            writeCaptureManifest(capture);
            {
                std::lock_guard<std::mutex> dataLock(dataMutex_);
                lastCapture_ = capture;
            }
            if (resumePreview) {
                startColorPreviewLocked();
            }
            return capture.texturePaths;
        } catch (...) {
            try {
                if (pipe_) {
                    pipe_->stop();
                }
            } catch (...) {
            }
            if (resumePreview) {
                startColorPreviewLocked();
            }
            throw;
        }
    }

private:
    /// 当前图片保存根目录。
    std::string imageRoot_;
    /// Orbbec 数据管线。
    std::unique_ptr<ob::Pipeline> pipe_;
    /// 当前连接的 Orbbec 设备。
    std::shared_ptr<ob::Device> device_;
    /// 保护 SDK 管线启停和采集流程。
    mutable std::mutex controlMutex_;
    /// 保护最新帧、缓存预览和采集结果。
    mutable std::mutex dataMutex_;
    /// 彩色预览后台线程。
    std::thread previewThread_;
    /// 彩色预览是否运行。
    std::atomic<bool> previewRunning_;
    /// 通知预览线程退出。
    std::atomic<bool> stopPreview_;
    /// 最近一帧彩色图像。
    CameraFrameData latestColor_;
    /// 最近一帧编码后的预览图缓存。
    CameraImage latestPreview_;
    /// 预览缓存对应的彩色帧序号。
    uint64_t latestPreviewFrameIndex_;
    /// 最近一次同步采集结果。
    SynchronizedCaptureFrames lastCapture_;

    /// 在持有控制锁时启动彩色预览。
    void startColorPreviewLocked()
    {
        if (previewRunning_) {
            return;
        }
        ensureDevice();
        stopPreview_ = false;
        pipe_->start(makeColorConfig());
        previewRunning_ = true;
        previewThread_ = std::thread(&OrbbecCameraDevice::previewLoop, this);
    }

    /// 在持有控制锁时停止彩色预览。
    void stopColorPreviewLocked()
    {
        if (!previewRunning_) {
            return;
        }
        stopPreview_ = true;
        if (previewThread_.joinable()) {
            previewThread_.join();
        }
        pipe_->stop();
        previewRunning_ = false;
        stopPreview_ = false;
    }

    /// 后台循环读取彩色帧并刷新最新帧缓存。
    void previewLoop()
    {
        for (;;) {
            if (stopPreview_) {
                break;
            }
            try {
                std::shared_ptr<ob::FrameSet> frameSet = pipe_->waitForFrameset(500);
                if (!frameSet) {
                    continue;
                }
                std::shared_ptr<ob::Frame> raw = frameSet->getFrame(OB_FRAME_COLOR);
                if (!raw) {
                    continue;
                }
                CameraFrameData color = copyColorFrame(raw->as<ob::ColorFrame>());
                if (color.valid()) {
                    std::lock_guard<std::mutex> lock(dataMutex_);
                    latestColor_ = color;
                    latestPreview_ = CameraImage();
                    latestPreviewFrameIndex_ = 0;
                }
            } catch (...) {
                std::this_thread::sleep_for(std::chrono::milliseconds(60));
            }
        }
    }

    /// 读取最近彩色帧的快照。
    CameraFrameData latestColorFrameLocked() const
    {
        std::lock_guard<std::mutex> lock(dataMutex_);
        return latestColor_;
    }

    /// 在超时时间内等待可用彩色帧。
    CameraFrameData waitForLatestColorLocked(int timeoutMs) const
    {
        const std::chrono::steady_clock::time_point deadline =
            std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
        while (std::chrono::steady_clock::now() < deadline) {
            CameraFrameData frame = latestColorFrameLocked();
            if (frame.valid()) {
                return frame;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(40));
        }
        return latestColorFrameLocked();
    }

    /// 切换红外通道并采集一帧红外图像。
    CameraFrameData captureIrFrameLocked(int channel, const std::string& stream)
    {
        setIrChannel(channel);
        pipe_->start(makeIrConfig());
        int timeouts = 0;
        try {
            while (timeouts < 20) {
                std::shared_ptr<ob::FrameSet> frameSet = pipe_->waitForFrameset(1000);
                if (!frameSet) {
                    ++timeouts;
                    continue;
                }
                CameraFrameData frame = copyIrFrame(getIrFrameFromSet(frameSet), stream);
                if (frame.valid()) {
                    pipe_->stop();
                    return frame;
                }
            }
            pipe_->stop();
        } catch (...) {
            try {
                if (pipe_) {
                    pipe_->stop();
                }
            } catch (...) {
            }
            throw;
        }
        throw std::runtime_error("Timeout while waiting for " + stream + " frame");
    }

    /// 设置 Orbbec 双目红外数据源通道。
    void setIrChannel(int channel)
    {
        ensureDevice();
        if (!device_->isPropertySupported(OB_PROP_IR_CHANNEL_DATA_SOURCE_INT, OB_PERMISSION_WRITE)) {
            throw std::runtime_error("OB_PROP_IR_CHANNEL_DATA_SOURCE_INT is not writable on this Orbbec device");
        }
        device_->setIntProperty(OB_PROP_IR_CHANNEL_DATA_SOURCE_INT, channel);
    }

    /// 延迟创建 SDK 管线并缓存设备句柄。
    void ensureDevice()
    {
        if (!pipe_) {
            pipe_.reset(new ob::Pipeline());
        }
        if (!device_) {
            device_ = pipe_->getDevice();
        }
    }

    /// 构造等待真实相机彩色流的占位 SVG。
    static std::string waitingSvg()
    {
        std::ostringstream os;
        os << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"640\" height=\"400\" viewBox=\"0 0 640 400\">";
        os << "<rect width=\"640\" height=\"400\" fill=\"#eef3f5\"/>";
        os << "<rect x=\"24\" y=\"24\" width=\"592\" height=\"352\" rx=\"10\" fill=\"none\" stroke=\"#9fb0bb\" stroke-width=\"3\"/>";
        os << "<text x=\"320\" y=\"204\" text-anchor=\"middle\" font-family=\"Arial, sans-serif\" font-size=\"22\" font-weight=\"700\" fill=\"#3f5663\">等待奥比中光彩色流</text>";
        os << "</svg>";
        return os.str();
    }
};
#endif

/// 根据配置创建相机设备实例。
std::unique_ptr<ICameraDevice> createCameraDevice(const std::string& imageRoot, const std::string& cameraMode)
{
#if defined(FACESCAN_WITH_ORBBEC)
    if (cameraMode == "orbbec" || cameraMode == "gemini215") {
        return std::unique_ptr<ICameraDevice>(new OrbbecCameraDevice(imageRoot));
    }
#else
    if (cameraMode == "orbbec" || cameraMode == "gemini215") {
        throw std::runtime_error("Orbbec SDK is not linked in this build");
    }
#endif
    return std::unique_ptr<ICameraDevice>(new MockCameraDevice(imageRoot));
}

} // namespace

/// 虚析构函数定义，确保派生设备可通过接口指针释放。
ICameraDevice::~ICameraDevice()
{
}

/// 默认使用模拟相机模式。
CameraManager::CameraManager(const std::string& imageRoot)
    : device_(createCameraDevice(imageRoot, "mock"))
{
}

/// 按配置模式创建相机设备。
CameraManager::CameraManager(const std::string& imageRoot, const std::string& cameraMode)
    : device_(createCameraDevice(imageRoot, cameraMode))
{
}

/// 注入设备实现，便于单元测试替换真实设备。
CameraManager::CameraManager(std::unique_ptr<ICameraDevice> device)
    : device_(std::move(device))
{
}

/// 启动当前设备预览。
void CameraManager::start()
{
    device_->start();
}

/// 停止当前设备预览。
void CameraManager::stop()
{
    device_->stop();
}

/// 查询当前设备预览状态。
bool CameraManager::streaming() const
{
    return device_->streaming();
}

/// 更新当前设备保存根目录。
void CameraManager::setImageRoot(const std::string& imageRoot)
{
    device_->setImageRoot(imageRoot);
}

/// 规范化目录路径，避免后续拼接出现重复分隔符。
std::string CameraManager::normalizeRoot(const std::string& path)
{
    if (path.empty()) {
        return "data/images";
    }
    if (path[path.size() - 1] == '/' || path[path.size() - 1] == '\\') {
        return path.substr(0, path.size() - 1);
    }
    return path;
}

/// 代理获取设备预览图。
CameraImage CameraManager::frameImage(const std::string& view)
{
    return device_->frameImage(view);
}

/// 代理执行设备采集。
std::vector<std::string> CameraManager::capture(const std::string& orderFolder, const std::string& orderName)
{
    return device_->capture(orderFolder, orderName);
}

} // namespace facescan
