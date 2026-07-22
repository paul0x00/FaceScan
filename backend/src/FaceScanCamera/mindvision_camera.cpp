#include "mindvision_camera.hpp"

#include "../common/time_utils.hpp"

#ifdef _WIN32
#include <windows.h>
#endif
#include <CameraApi.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>

namespace facescan {
namespace {

const int kTriggerModeSettleMs = 30;
const int kTriggerWarmupSettleMs = 20;
const int kTriggerWarmupExtraMs = 180;

std::string deviceName(const tSdkCameraDevInfo& info)
{
    std::string name = info.acProductName;
    if (name.empty()) name = info.acFriendlyName;
    return name;
}

CameraControlRange makeControlRange(
    const std::string& key,
    const std::string& label,
    bool supported,
    int value,
    int minValue,
    int maxValue,
    int step)
{
    CameraControlRange range;
    range.key = key;
    range.label = label;
    range.supported = supported;
    range.writable = supported;
    range.value = value;
    range.min = minValue;
    range.max = maxValue;
    range.step = std::max(1, step);
    range.defaultValue = value;
    return range;
}

class MindvisionVendorCamera : public IVendorCamera {
public:
    MindvisionVendorCamera(CameraHandle handle, const DiscoveredCamera& info, bool mono)
        : handle_(handle), info_(info), mono_(mono), streaming_(false), triggerMode_(false),
          triggerWarmupNeeded_(false), frameCount_(0)
    {
    }

    ~MindvisionVendorCamera()
    {
        stopStream();
        if (handle_ != 0) {
            CameraUnInit(handle_);
            handle_ = 0;
        }
    }

    const std::string& vendor() const { return info_.vendor; }
    const std::string& name() const { return info_.name; }
    const std::string& serial() const { return info_.serial; }

    bool startStream(bool triggerMode)
    {
        std::lock_guard<std::mutex> lock(sdkMutex_);
        if (handle_ == 0) return false;
        if (streaming_) return true;
        if (CameraSetTriggerMode(handle_, triggerMode ? 1 : 0) != CAMERA_STATUS_SUCCESS) return false;
        if (triggerMode) {
            if (CameraSetTriggerCount(handle_, 1) != CAMERA_STATUS_SUCCESS) return false;
            CameraClearBuffer(handle_);
            triggerWarmupNeeded_ = true;
        } else {
            triggerWarmupNeeded_ = false;
        }
        triggerMode_ = triggerMode;
        if (CameraSetCallbackFunction(handle_, &MindvisionVendorCamera::frameCallback, this, 0) != CAMERA_STATUS_SUCCESS) return false;
        if (CameraPlay(handle_) != CAMERA_STATUS_SUCCESS) return false;
        streaming_ = true;
        return true;
    }

    void stopStream()
    {
        std::lock_guard<std::mutex> lock(sdkMutex_);
        if (handle_ != 0 && streaming_) CameraPause(handle_);
        streaming_ = false;
    }

    bool streaming() const { return streaming_.load(); }
    CameraFrameData latestColorFrame() const { return CameraFrameData(); }

    CameraControlState controls()
    {
        std::lock_guard<std::mutex> lock(sdkMutex_);
        return controlsUnlocked();
    }

    CameraControlState updateControls(const CameraControlUpdate& update)
    {
        std::lock_guard<std::mutex> lock(sdkMutex_);
        if (handle_ == 0) return controlsUnlocked();
        if (update.hasAutoExposure) {
            CameraSetAeState(handle_, update.autoExposure ? 1 : 0);
        }
        if (update.hasExposure) {
            double minUs = 1000.0;
            double maxUs = 100000.0;
            double stepUs = 1000.0;
            CameraGetExposureTimeRange(handle_, &minUs, &maxUs, &stepUs);
            const double requestedUs = static_cast<double>(std::max(1, std::min(100, update.exposure))) * 1000.0;
            CameraSetExposureTime(handle_, std::max(minUs, std::min(maxUs, requestedUs)));
        }
        if (update.hasGain) {
            float minGain = 1.0f;
            float maxGain = 20.0f;
            float stepGain = 1.0f;
            CameraGetAnalogGainXRange(handle_, &minGain, &maxGain, &stepGain);
            const float requested = static_cast<float>(std::max(1, std::min(20, update.gain)));
            CameraSetAnalogGainX(handle_, std::max(minGain, std::min(maxGain, requested)));
        }
        return controlsUnlocked();
    }

    bool captureTriggerSet(VendorTriggerShot& out, TriggerBarrier& barrier, int timeoutMs, bool firstInBatch)
    {
        timeoutMs = timeoutMs > 0 ? timeoutMs : 1000;
        const bool restoreContinuous = !triggerMode_.load();
        struct Restorer {
            MindvisionVendorCamera* camera;
            bool restore;
            ~Restorer()
            {
                if (!restore) return;
                std::lock_guard<std::mutex> lock(camera->sdkMutex_);
                if (camera->handle_ != 0) CameraSetTriggerMode(camera->handle_, 0);
                camera->triggerMode_ = false;
            }
        } restorer = { this, restoreContinuous };

        bool ready = triggerMode_.load();
        if (!ready) {
            std::lock_guard<std::mutex> lock(sdkMutex_);
            const bool modeOk = handle_ != 0 && CameraSetTriggerMode(handle_, 1) == CAMERA_STATUS_SUCCESS;
            const bool countOk = handle_ != 0 && CameraSetTriggerCount(handle_, 1) == CAMERA_STATUS_SUCCESS;
            ready = modeOk && countOk;
            if (ready) {
                CameraClearBuffer(handle_);
                triggerWarmupNeeded_ = true;
            }
            triggerMode_ = ready;
        }
        if (ready && restoreContinuous) std::this_thread::sleep_for(std::chrono::milliseconds(kTriggerModeSettleMs));

        if (ready && (firstInBatch || triggerWarmupNeeded_.load())) {
            VendorTriggerShot discarded;
            if (!softTriggerAndWait(warmupTimeout(timeoutMs), discarded)) {
                triggerWarmupNeeded_ = true;
                barrier.arrive();
                barrier.wait(timeoutMs);
                out.diagnostic = "MindVision warm-up trigger timed out";
                out.ok = false;
                return false;
            }
            triggerWarmupNeeded_ = false;
            std::this_thread::sleep_for(std::chrono::milliseconds(kTriggerWarmupSettleMs));
        }

        barrier.arrive();
        if (!barrier.wait(timeoutMs) || !ready) {
            out.diagnostic = "MindVision trigger barrier cancelled or trigger mode unavailable";
            out.ok = false;
            return false;
        }
        if (!softTriggerAndWait(timeoutMs, out)) {
            out.diagnostic = "Timed out waiting for MindVision stereo frame";
            out.ok = false;
            return false;
        }
        out.ok = out.left.valid() && out.right.valid();
        return out.ok;
    }

private:
    CameraHandle handle_;
    DiscoveredCamera info_;
    bool mono_;
    mutable std::mutex sdkMutex_;
    mutable std::mutex frameMutex_;
    std::condition_variable frameCv_;
    std::vector<unsigned char> ispBuffer_;
    std::atomic<bool> streaming_;
    std::atomic<bool> triggerMode_;
    std::atomic<bool> triggerWarmupNeeded_;
    std::atomic<uint64_t> frameCount_;
    CameraFrameData latestLeft_;
    CameraFrameData latestRight_;

    CameraControlState controlsUnlocked()
    {
        CameraControlState state;
        state.exposure = makeControlRange("exposure", "曝光时间 (ms)", false, 0, 1, 100, 1);
        state.gain = makeControlRange("gain", "增益 (倍)", false, 0, 1, 20, 1);
        state.brightness.key = "brightness";
        state.brightness.label = "亮度";
        if (handle_ == 0) return state;

        BOOL autoExposure = 0;
        if (CameraGetAeState(handle_, &autoExposure) == CAMERA_STATUS_SUCCESS) {
            state.autoExposureSupported = true;
            state.autoExposureWritable = true;
            state.autoExposure = autoExposure != 0;
        }

        double minUs = 0.0;
        double maxUs = 0.0;
        double stepUs = 0.0;
        double currentUs = 0.0;
        if (CameraGetExposureTimeRange(handle_, &minUs, &maxUs, &stepUs) == CAMERA_STATUS_SUCCESS
            && CameraGetExposureTime(handle_, &currentUs) == CAMERA_STATUS_SUCCESS) {
            const int minMs = std::min(100, std::max(1, static_cast<int>(std::ceil(minUs / 1000.0))));
            const int maxMs = std::max(minMs, std::min(100, static_cast<int>(std::floor(maxUs / 1000.0))));
            const int valueMs = std::max(minMs, std::min(maxMs, static_cast<int>(std::floor(currentUs / 1000.0 + 0.5))));
            state.exposure = makeControlRange("exposure", "曝光时间 (ms)", true, valueMs, minMs, maxMs,
                std::max(1, static_cast<int>(std::ceil(stepUs / 1000.0))));
        }

        float minGain = 0.0f;
        float maxGain = 0.0f;
        float stepGain = 0.0f;
        float currentGain = 0.0f;
        if (CameraGetAnalogGainXRange(handle_, &minGain, &maxGain, &stepGain) == CAMERA_STATUS_SUCCESS
            && CameraGetAnalogGainX(handle_, &currentGain) == CAMERA_STATUS_SUCCESS) {
            const int minValue = std::min(20, std::max(1, static_cast<int>(std::ceil(minGain))));
            const int maxValue = std::max(minValue, std::min(20, static_cast<int>(std::floor(maxGain))));
            const int value = std::max(minValue, std::min(maxValue, static_cast<int>(std::floor(currentGain + 0.5f))));
            state.gain = makeControlRange("gain", "增益 (倍)", true, value, minValue, maxValue,
                std::max(1, static_cast<int>(std::ceil(stepGain))));
        }
        return state;
    }

    static void __stdcall frameCallback(CameraHandle handle, BYTE* data, tSdkFrameHead* head, PVOID user)
    {
        (void)handle;
        if (user) static_cast<MindvisionVendorCamera*>(user)->onFrame(data, head);
    }

    void onFrame(BYTE* data, tSdkFrameHead* head)
    {
        if (!data || !head || head->iWidth <= 1 || head->iHeight <= 0) return;
        try {
            const int width = head->iWidth;
            const int height = head->iHeight;
            const int bytesPerPixel = mono_ ? 1 : 3;
            const std::size_t required = static_cast<std::size_t>(width) * height * bytesPerPixel;
            if (ispBuffer_.size() < required) ispBuffer_.resize(required);
            tSdkFrameHead output = *head;
            if (CameraImageProcess(handle_, data, ispBuffer_.data(), &output) != CAMERA_STATUS_SUCCESS) return;

            const int leftWidth = width / 2;
            const int rightWidth = width - leftWidth;
            const uint64_t index = frameCount_.load() + 1u;
            CameraFrameData left = grayHalf(0, leftWidth, width, height, bytesPerPixel, index, "left");
            CameraFrameData right = grayHalf(leftWidth, rightWidth, width, height, bytesPerPixel, index, "right");
            if (!left.valid() || !right.valid()) return;
            {
                std::lock_guard<std::mutex> lock(frameMutex_);
                latestLeft_ = left;
                latestRight_ = right;
                frameCount_ = index;
            }
            frameCv_.notify_all();
        } catch (...) {
        }
    }

    CameraFrameData grayHalf(
        int startX,
        int halfWidth,
        int fullWidth,
        int height,
        int bytesPerPixel,
        uint64_t frameIndex,
        const std::string& stream) const
    {
        CameraFrameData frame;
        if (halfWidth <= 0) return frame;
        frame.stream = stream;
        frame.format = "Y8";
        frame.width = static_cast<uint32_t>(halfWidth);
        frame.height = static_cast<uint32_t>(height);
        frame.frameIndex = frameIndex;
        frame.capturedAt = nowText();
        frame.data.resize(static_cast<std::size_t>(halfWidth) * height);
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < halfWidth; ++x) {
                const std::size_t source = (static_cast<std::size_t>(y) * fullWidth + startX + x) * bytesPerPixel;
                unsigned char gray = ispBuffer_[source];
                if (bytesPerPixel == 3) {
                    const unsigned int r = ispBuffer_[source];
                    const unsigned int g = ispBuffer_[source + 1u];
                    const unsigned int b = ispBuffer_[source + 2u];
                    gray = static_cast<unsigned char>((77u * r + 150u * g + 29u * b) >> 8);
                }
                frame.data[static_cast<std::size_t>(y) * halfWidth + x] = gray;
            }
        }
        return frame;
    }

    int warmupTimeout(int timeoutMs)
    {
        double exposureUs = 0.0;
        {
            std::lock_guard<std::mutex> lock(sdkMutex_);
            if (handle_ != 0) CameraGetExposureTime(handle_, &exposureUs);
        }
        const int exposureMs = exposureUs > 0.0 ? static_cast<int>(std::ceil(exposureUs / 1000.0)) : 0;
        return std::max(timeoutMs, exposureMs + kTriggerWarmupExtraMs);
    }

    bool softTriggerAndWait(int timeoutMs, VendorTriggerShot& shot)
    {
        uint64_t baseline = 0;
        {
            std::lock_guard<std::mutex> lock(sdkMutex_);
            if (handle_ == 0) return false;
            CameraClearBuffer(handle_);
            baseline = frameCount_.load();
            if (CameraSoftTrigger(handle_) != CAMERA_STATUS_SUCCESS) return false;
        }
        std::unique_lock<std::mutex> frameLock(frameMutex_);
        const bool received = frameCv_.wait_for(frameLock, std::chrono::milliseconds(timeoutMs), [this, baseline]() {
            return frameCount_.load() > baseline && latestLeft_.valid() && latestRight_.valid();
        });
        if (received) {
            shot.left = latestLeft_;
            shot.right = latestRight_;
        }
        return received;
    }
};

} // namespace

MindvisionBackend::MindvisionBackend()
{
    CameraSdkInit(1);
}

MindvisionBackend::~MindvisionBackend() {}

std::vector<DiscoveredCamera> MindvisionBackend::discover(std::string& error)
{
    std::vector<DiscoveredCamera> result;
    tSdkCameraDevInfo devices[16];
    std::memset(devices, 0, sizeof(devices));
    int count = 16;
    const int rc = CameraEnumerateDevice(devices, &count);
    if (rc != CAMERA_STATUS_SUCCESS) {
        error = "Failed to enumerate MindVision cameras: " + std::to_string(rc);
        return result;
    }
    for (int i = 0; i < count; ++i) {
        DiscoveredCamera camera;
        camera.vendor = vendor();
        camera.name = deviceName(devices[i]);
        camera.serial = devices[i].acSn;
        camera.connectionType = devices[i].acPortType;
        if (!camera.serial.empty()) result.push_back(camera);
    }
    if (result.empty()) error = "MindVision SDK found no cameras";
    return result;
}

std::shared_ptr<IVendorCamera> MindvisionBackend::open(const std::string& serial, std::string& error)
{
    tSdkCameraDevInfo devices[16];
    std::memset(devices, 0, sizeof(devices));
    int count = 16;
    if (CameraEnumerateDevice(devices, &count) != CAMERA_STATUS_SUCCESS) {
        error = "Failed to enumerate MindVision cameras";
        return std::shared_ptr<IVendorCamera>();
    }
    for (int i = 0; i < count; ++i) {
        if (serial != devices[i].acSn) continue;
        CameraHandle handle = 0;
        const int rc = CameraInit(&devices[i], -1, -1, &handle);
        if (rc != CAMERA_STATUS_SUCCESS) {
            error = "Failed to open MindVision camera " + serial + ": " + std::to_string(rc);
            return std::shared_ptr<IVendorCamera>();
        }
        tSdkCameraCapbility capability;
        std::memset(&capability, 0, sizeof(capability));
        bool mono = false;
        if (CameraGetCapability(handle, &capability) == CAMERA_STATUS_SUCCESS) {
            mono = capability.sIspCapacity.bMonoSensor != 0;
        }
        CameraSetIspOutFormat(handle, mono ? CAMERA_MEDIA_TYPE_MONO8 : CAMERA_MEDIA_TYPE_RGB8);
        DiscoveredCamera camera;
        camera.vendor = vendor();
        camera.name = deviceName(devices[i]);
        camera.serial = devices[i].acSn;
        camera.connectionType = devices[i].acPortType;
        return std::shared_ptr<IVendorCamera>(new MindvisionVendorCamera(handle, camera, mono));
    }
    error = "MindVision camera not found: " + serial;
    return std::shared_ptr<IVendorCamera>();
}

} // namespace facescan
