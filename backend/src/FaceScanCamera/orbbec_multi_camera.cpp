#include "orbbec_multi_camera.hpp"

#include "../common/time_utils.hpp"

#include <libobsensor/ObSensor.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <mutex>
#include <stdexcept>
#include <thread>

namespace facescan {
namespace {

enum class OrbbecStreamMode { Color, IrLeft, IrRight };

CameraControlRange makeRange(const std::string& key, const std::string& label, bool supported, bool writable,
                             int value, int minValue, int maxValue, int step, int defaultValue)
{
    CameraControlRange range;
    range.key = key;
    range.label = label;
    range.supported = supported;
    range.writable = writable;
    range.value = value;
    range.min = minValue;
    range.max = maxValue;
    range.step = step > 0 ? step : 1;
    range.defaultValue = defaultValue;
    return range;
}

CameraFrameData colorFrameData(const std::shared_ptr<ob::ColorFrame>& frame)
{
    CameraFrameData out;
    if (!frame) return out;
    const uint32_t width = frame->getWidth();
    const uint32_t height = frame->getHeight();
    const uint8_t* src = frame->getData();
    const uint32_t size = frame->getDataSize();
    if (!src || width == 0 || height == 0 || size < width * height * 3u) return out;
    out.stream = "color";
    out.format = "RGB8";
    out.width = width;
    out.height = height;
    out.frameIndex = frame->getIndex();
    out.capturedAt = nowText();
    out.data.assign(src, src + static_cast<std::size_t>(width) * height * 3u);
    if (frame->getFormat() == OB_FORMAT_BGR) {
        for (std::size_t i = 0; i + 2 < out.data.size(); i += 3) {
            std::swap(out.data[i], out.data[i + 2]);
        }
    }
    return out;
}

CameraFrameData irFrameData(const std::shared_ptr<ob::IRFrame>& frame, const std::string& stream)
{
    CameraFrameData out;
    if (!frame) return out;
    const uint32_t width = frame->getWidth();
    const uint32_t height = frame->getHeight();
    const uint8_t* src = frame->getData();
    const uint32_t size = frame->getDataSize();
    const std::size_t pixels = static_cast<std::size_t>(width) * height;
    if (!src || width == 0 || height == 0 || size < pixels) return out;
    out.stream = stream;
    out.format = "Y8";
    out.width = width;
    out.height = height;
    out.frameIndex = frame->getIndex();
    out.capturedAt = nowText();
    out.data.resize(pixels);
    if (frame->getFormat() == OB_FORMAT_Y16 && size >= pixels * 2u) {
        for (std::size_t i = 0; i < pixels; ++i) {
            uint16_t value = 0;
            std::memcpy(&value, src + i * 2u, sizeof(value));
            out.data[i] = static_cast<unsigned char>(value >> 8);
        }
    } else {
        std::copy(src, src + pixels, out.data.begin());
    }
    return out;
}

std::shared_ptr<ob::IRFrame> findIrFrame(const std::shared_ptr<ob::FrameSet>& frames)
{
    if (!frames) return std::shared_ptr<ob::IRFrame>();
    std::shared_ptr<ob::Frame> direct = frames->getFrame(OB_FRAME_IR);
    if (direct) return direct->as<ob::IRFrame>();
    const uint32_t count = frames->getCount();
    for (uint32_t i = 0; i < count; ++i) {
        std::shared_ptr<ob::Frame> frame = frames->getFrameByIndex(i);
        if (!frame) continue;
        const OBFrameType type = frame->getType();
        if (type == OB_FRAME_IR || type == OB_FRAME_IR_LEFT || type == OB_FRAME_IR_RIGHT) {
            return frame->as<ob::IRFrame>();
        }
    }
    return std::shared_ptr<ob::IRFrame>();
}

class OrbbecVendorCamera : public IVendorCamera {
public:
    OrbbecVendorCamera(const std::shared_ptr<ob::Context>& context, const std::shared_ptr<ob::Device>& device,
                       const DiscoveredCamera& info)
        : context_(context), device_(device), pipeline_(new ob::Pipeline(device)), name_(info.name), serial_(info.serial),
          streamStarted_(false), previewRunning_(false), streaming_(false), mode_(OrbbecStreamMode::Color)
    {
    }

    ~OrbbecVendorCamera()
    {
        stopStream();
    }

    const std::string& vendor() const { return vendor_; }
    const std::string& name() const { return name_; }
    const std::string& serial() const { return serial_; }

    bool startStream(bool)
    {
        stopPreviewThread();
        std::lock_guard<std::mutex> lock(sdkMutex_);
        if (!startModeLocked(OrbbecStreamMode::Color)) return false;
        streaming_ = true;
        startPreviewThread();
        return true;
    }

    void stopStream()
    {
        stopPreviewThread();
        std::lock_guard<std::mutex> lock(sdkMutex_);
        if (streamStarted_) {
            try { pipeline_->stop(); } catch (...) {}
            streamStarted_ = false;
        }
        streaming_ = false;
    }

    bool streaming() const { return streaming_; }

    CameraFrameData latestColorFrame() const
    {
        std::lock_guard<std::mutex> lock(frameMutex_);
        return latestColor_;
    }

    CameraControlState controls()
    {
        std::lock_guard<std::mutex> lock(sdkMutex_);
        CameraControlState state;
        state.autoExposureSupported = device_->isPropertySupported(OB_PROP_COLOR_AUTO_EXPOSURE_BOOL, OB_PERMISSION_READ)
            || device_->isPropertySupported(OB_PROP_IR_AUTO_EXPOSURE_BOOL, OB_PERMISSION_READ);
        state.autoExposureWritable = device_->isPropertySupported(OB_PROP_COLOR_AUTO_EXPOSURE_BOOL, OB_PERMISSION_WRITE)
            || device_->isPropertySupported(OB_PROP_IR_AUTO_EXPOSURE_BOOL, OB_PERMISSION_WRITE);
        state.autoExposure = state.autoExposureSupported
            ? (device_->isPropertySupported(OB_PROP_COLOR_AUTO_EXPOSURE_BOOL, OB_PERMISSION_READ)
                ? device_->getBoolProperty(OB_PROP_COLOR_AUTO_EXPOSURE_BOOL)
                : device_->getBoolProperty(OB_PROP_IR_AUTO_EXPOSURE_BOOL))
            : false;
        state.exposure = intRangeMs(OB_PROP_COLOR_EXPOSURE_INT, "exposure", "曝光时间 (ms)");
        state.gain = gainRange(OB_PROP_COLOR_GAIN_INT);
        state.brightness = intRange(OB_PROP_COLOR_BRIGHTNESS_INT, "brightness", "亮度");
        return state;
    }

    CameraControlState updateControls(const CameraControlUpdate& update)
    {
        std::lock_guard<std::mutex> lock(sdkMutex_);
        if (update.hasAutoExposure) {
            setBoolIfSupported(OB_PROP_COLOR_AUTO_EXPOSURE_BOOL, update.autoExposure);
            setBoolIfSupported(OB_PROP_IR_AUTO_EXPOSURE_BOOL, update.autoExposure);
        }
        if (update.hasExposure) {
            setIntMs(OB_PROP_COLOR_EXPOSURE_INT, update.exposure);
            setIntMs(OB_PROP_IR_EXPOSURE_INT, update.exposure);
        }
        if (update.hasGain) {
            setInt(OB_PROP_COLOR_GAIN_INT, std::max(1, std::min(20, update.gain)));
            setInt(OB_PROP_IR_GAIN_INT, std::max(1, std::min(20, update.gain)));
        }
        if (update.hasBrightness) setInt(OB_PROP_COLOR_BRIGHTNESS_INT, update.brightness);
        return controlsUnlocked();
    }

    bool captureTriggerSet(VendorTriggerShot& out, TriggerBarrier& barrier, int timeoutMs, bool)
    {
        barrier.arrive();
        if (!barrier.wait(timeoutMs > 0 ? timeoutMs : 1000)) {
            out.ok = false;
            out.diagnostic = "Orbbec trigger barrier timeout";
            return false;
        }
        stopPreviewThread();
        std::lock_guard<std::mutex> lock(sdkMutex_);
        {
            std::lock_guard<std::mutex> frameLock(frameMutex_);
            out.color = latestColor_;
        }
        try {
            out.left = captureIrLocked(0, "leftIr", timeoutMs);
            out.right = captureIrLocked(1, "rightIr", timeoutMs);
        } catch (const std::exception& e) {
            out.diagnostic = e.what();
        }
        const bool restored = startModeLocked(OrbbecStreamMode::Color);
        if (restored) {
            startPreviewThread();
        } else {
            streaming_ = false;
            if (!out.diagnostic.empty()) out.diagnostic += "; ";
            out.diagnostic += "Failed to restore Orbbec color preview";
        }
        out.ok = restored && out.color.valid() && out.left.valid() && out.right.valid();
        if (!out.ok && out.diagnostic.empty()) {
            out.diagnostic = "Incomplete Orbbec color/left IR/right IR trigger set";
        }
        return out.ok;
    }

private:
    std::shared_ptr<ob::Context> context_;
    std::shared_ptr<ob::Device> device_;
    std::unique_ptr<ob::Pipeline> pipeline_;
    std::string vendor_ = "Orbbec";
    std::string name_;
    std::string serial_;
    mutable std::mutex sdkMutex_;
    mutable std::mutex frameMutex_;
    bool streamStarted_;
    std::atomic<bool> previewRunning_;
    std::atomic<bool> streaming_;
    OrbbecStreamMode mode_;
    std::thread previewThread_;
    CameraFrameData latestColor_;

    CameraControlRange intRange(OBPropertyID id, const std::string& key, const std::string& label)
    {
        const bool readable = device_->isPropertySupported(id, OB_PERMISSION_READ);
        const bool writable = device_->isPropertySupported(id, OB_PERMISSION_WRITE);
        if (!readable) return makeRange(key, label, false, writable, 0, 0, 100, 1, 0);
        const OBIntPropertyRange range = device_->getIntPropertyRange(id);
        return makeRange(key, label, true, writable, device_->getIntProperty(id), range.min, range.max, range.step, range.def);
    }

    static int microsecondsToMilliseconds(int value)
    {
        return std::max(1, value / 1000);
    }

    CameraControlRange intRangeMs(OBPropertyID id, const std::string& key, const std::string& label)
    {
        const bool readable = device_->isPropertySupported(id, OB_PERMISSION_READ);
        const bool writable = device_->isPropertySupported(id, OB_PERMISSION_WRITE);
        if (!readable) return makeRange(key, label, false, writable, 0, 1, 100, 1, 1);
        const OBIntPropertyRange range = device_->getIntPropertyRange(id);
        const int minValue = std::min(100, std::max(1, microsecondsToMilliseconds(range.min)));
        const int maxValue = std::max(minValue, std::min(100, microsecondsToMilliseconds(range.max)));
        const int value = std::max(minValue, std::min(maxValue, microsecondsToMilliseconds(device_->getIntProperty(id))));
        const int defaultValue = std::max(minValue, std::min(maxValue, microsecondsToMilliseconds(range.def)));
        return makeRange(key, label, true, writable, value, minValue, maxValue,
            std::max(1, microsecondsToMilliseconds(range.step)), defaultValue);
    }

    CameraControlRange gainRange(OBPropertyID id)
    {
        CameraControlRange range = intRange(id, "gain", "增益 (倍)");
        if (!range.supported) {
            range.min = 1;
            range.max = 20;
            range.step = 1;
            return range;
        }
        range.min = std::min(20, std::max(1, range.min));
        range.max = std::max(range.min, std::min(20, range.max));
        range.value = std::max(range.min, std::min(range.max, range.value));
        range.defaultValue = std::max(range.min, std::min(range.max, range.defaultValue));
        range.step = std::max(1, range.step);
        return range;
    }

    CameraControlState controlsUnlocked()
    {
        CameraControlState state;
        state.autoExposureSupported = device_->isPropertySupported(OB_PROP_COLOR_AUTO_EXPOSURE_BOOL, OB_PERMISSION_READ)
            || device_->isPropertySupported(OB_PROP_IR_AUTO_EXPOSURE_BOOL, OB_PERMISSION_READ);
        state.autoExposureWritable = device_->isPropertySupported(OB_PROP_COLOR_AUTO_EXPOSURE_BOOL, OB_PERMISSION_WRITE)
            || device_->isPropertySupported(OB_PROP_IR_AUTO_EXPOSURE_BOOL, OB_PERMISSION_WRITE);
        state.autoExposure = state.autoExposureSupported
            ? (device_->isPropertySupported(OB_PROP_COLOR_AUTO_EXPOSURE_BOOL, OB_PERMISSION_READ)
                ? device_->getBoolProperty(OB_PROP_COLOR_AUTO_EXPOSURE_BOOL)
                : device_->getBoolProperty(OB_PROP_IR_AUTO_EXPOSURE_BOOL))
            : false;
        state.exposure = intRangeMs(OB_PROP_COLOR_EXPOSURE_INT, "exposure", "曝光时间 (ms)");
        state.gain = gainRange(OB_PROP_COLOR_GAIN_INT);
        state.brightness = intRange(OB_PROP_COLOR_BRIGHTNESS_INT, "brightness", "亮度");
        return state;
    }

    void setBoolIfSupported(OBPropertyID id, bool value)
    {
        if (device_->isPropertySupported(id, OB_PERMISSION_WRITE)) {
            device_->setBoolProperty(id, value);
        }
    }

    void setInt(OBPropertyID id, int value)
    {
        if (!device_->isPropertySupported(id, OB_PERMISSION_WRITE)) return;
        if (device_->isPropertySupported(id, OB_PERMISSION_READ)) {
            const OBIntPropertyRange range = device_->getIntPropertyRange(id);
            value = std::max(range.min, std::min(range.max, value));
        }
        device_->setIntProperty(id, value);
    }

    void setIntMs(OBPropertyID id, int milliseconds)
    {
        const int value = std::max(1, std::min(100, milliseconds));
        setInt(id, value * 1000);
    }

    bool startModeLocked(OrbbecStreamMode mode)
    {
        try {
            if (streamStarted_) {
                pipeline_->stop();
                streamStarted_ = false;
                std::this_thread::sleep_for(std::chrono::milliseconds(30));
            }
            std::shared_ptr<ob::Config> config(new ob::Config());
            if (mode == OrbbecStreamMode::Color) {
                config->enableVideoStream(OB_SENSOR_COLOR, OB_WIDTH_ANY, OB_HEIGHT_ANY, OB_FPS_ANY, OB_FORMAT_RGB);
            } else {
                device_->setIntProperty(OB_PROP_IR_CHANNEL_DATA_SOURCE_INT, mode == OrbbecStreamMode::IrLeft ? 0 : 1);
                config->enableVideoStream(OB_SENSOR_IR, OB_WIDTH_ANY, OB_HEIGHT_ANY, OB_FPS_ANY, OB_FORMAT_ANY);
            }
            pipeline_->start(config);
            streamStarted_ = true;
            mode_ = mode;
            return true;
        } catch (...) {
            streamStarted_ = false;
            return false;
        }
    }

    CameraFrameData captureIrLocked(int channel, const std::string& stream, int timeoutMs)
    {
        if (!startModeLocked(channel == 0 ? OrbbecStreamMode::IrLeft : OrbbecStreamMode::IrRight))
            throw std::runtime_error("Failed to start Orbbec " + stream + " stream");
        const int totalMs = std::max(timeoutMs, 1000);
        const std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(totalMs);
        while (std::chrono::steady_clock::now() < deadline) {
            std::shared_ptr<ob::FrameSet> frames = pipeline_->waitForFrameset(100);
            CameraFrameData frame = irFrameData(findIrFrame(frames), stream);
            if (frame.valid()) return frame;
        }
        throw std::runtime_error("Timeout waiting for Orbbec " + stream + " frame");
    }

    void startPreviewThread()
    {
        if (previewRunning_) return;
        previewRunning_ = true;
        previewThread_ = std::thread(&OrbbecVendorCamera::previewLoop, this);
    }

    void stopPreviewThread()
    {
        previewRunning_ = false;
        if (previewThread_.joinable()) previewThread_.join();
    }

    void previewLoop()
    {
        while (previewRunning_) {
            try {
                std::lock_guard<std::mutex> lock(sdkMutex_);
                if (!previewRunning_ || !streamStarted_ || mode_ != OrbbecStreamMode::Color) continue;
                std::shared_ptr<ob::FrameSet> frames = pipeline_->waitForFrameset(100);
                if (!frames) continue;
                std::shared_ptr<ob::Frame> raw = frames->getFrame(OB_FRAME_COLOR);
                if (!raw) continue;
                CameraFrameData frame = colorFrameData(raw->as<ob::ColorFrame>());
                if (frame.valid()) {
                    std::lock_guard<std::mutex> frameLock(frameMutex_);
                    latestColor_ = frame;
                }
            } catch (...) {
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
            }
        }
    }
};

} // namespace

OrbbecMultiBackend::OrbbecMultiBackend()
    : context_(new ob::Context())
{
    try { context_->enableDeviceClockSync(0); } catch (...) {}
}

OrbbecMultiBackend::~OrbbecMultiBackend() {}

std::vector<DiscoveredCamera> OrbbecMultiBackend::discover(std::string& error)
{
    std::vector<DiscoveredCamera> result;
    try {
        std::shared_ptr<ob::DeviceList> list = context_->queryDeviceList();
        const uint32_t count = list ? list->getCount() : 0;
        for (uint32_t i = 0; i < count; ++i) {
            DiscoveredCamera camera;
            camera.vendor = vendor();
            camera.name = list->getName(i) ? list->getName(i) : "";
            camera.serial = list->getSerialNumber(i) ? list->getSerialNumber(i) : "";
            camera.connectionType = list->getConnectionType(i) ? list->getConnectionType(i) : "";
            if (!camera.serial.empty()) result.push_back(camera);
        }
        if (result.empty()) error = "Orbbec SDK found no cameras";
    } catch (const std::exception& e) {
        error = std::string("Failed to discover Orbbec cameras: ") + e.what();
    }
    return result;
}

std::shared_ptr<IVendorCamera> OrbbecMultiBackend::open(const std::string& serial, std::string& error)
{
    try {
        std::shared_ptr<ob::DeviceList> list = context_->queryDeviceList();
        const uint32_t count = list ? list->getCount() : 0;
        for (uint32_t i = 0; i < count; ++i) {
            const char* current = list->getSerialNumber(i);
            if (!current || serial != current) continue;
            DiscoveredCamera info;
            info.vendor = vendor();
            info.name = list->getName(i) ? list->getName(i) : "";
            info.serial = current;
            info.connectionType = list->getConnectionType(i) ? list->getConnectionType(i) : "";
            return std::shared_ptr<IVendorCamera>(new OrbbecVendorCamera(context_, list->getDevice(i), info));
        }
        error = "Orbbec camera not found: " + serial;
    } catch (const std::exception& e) {
        error = std::string("Failed to open Orbbec camera: ") + e.what();
    }
    return std::shared_ptr<IVendorCamera>();
}

} // namespace facescan
