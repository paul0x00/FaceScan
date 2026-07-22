#include "hikvision_camera.hpp"

#include "../common/time_utils.hpp"

#include <MvCameraControl.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>

namespace facescan {
namespace {

const int kTriggerModeSettleMs = 30;

std::string fixedString(const unsigned char* value, std::size_t capacity)
{
    if (!value) return "";
    std::size_t length = 0;
    while (length < capacity && value[length] != 0) ++length;
    return std::string(reinterpret_cast<const char*>(value), length);
}

std::string lowerAscii(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

std::string manufacturerName(const MV_CC_DEVICE_INFO* info)
{
    if (!info) return "";
    if (info->nTLayerType == MV_USB_DEVICE) {
        const MV_USB3_DEVICE_INFO& usb = info->SpecialInfo.stUsb3VInfo;
        std::string value = fixedString(usb.chManufacturerName, sizeof(usb.chManufacturerName));
        if (value.empty()) value = fixedString(usb.chVendorName, sizeof(usb.chVendorName));
        return value;
    }
    if (info->nTLayerType == MV_GIGE_DEVICE) {
        const MV_GIGE_DEVICE_INFO& gige = info->SpecialInfo.stGigEInfo;
        return fixedString(gige.chManufacturerName, sizeof(gige.chManufacturerName));
    }
    return "";
}

bool discoveredFrom(const MV_CC_DEVICE_INFO* info, DiscoveredCamera& camera)
{
    if (!info) return false;
    const std::string manufacturer = lowerAscii(manufacturerName(info));
    if (manufacturer.find("hikvision") == std::string::npos &&
        manufacturer.find("hikrobot") == std::string::npos) {
        return false;
    }

    camera.vendor = "Hikvision";
    if (info->nTLayerType == MV_USB_DEVICE) {
        const MV_USB3_DEVICE_INFO& usb = info->SpecialInfo.stUsb3VInfo;
        camera.name = fixedString(usb.chModelName, sizeof(usb.chModelName));
        camera.serial = fixedString(usb.chSerialNumber, sizeof(usb.chSerialNumber));
        camera.connectionType = "USB3";
    } else if (info->nTLayerType == MV_GIGE_DEVICE) {
        const MV_GIGE_DEVICE_INFO& gige = info->SpecialInfo.stGigEInfo;
        camera.name = fixedString(gige.chModelName, sizeof(gige.chModelName));
        camera.serial = fixedString(gige.chSerialNumber, sizeof(gige.chSerialNumber));
        camera.connectionType = "GigE";
    } else {
        return false;
    }
    return !camera.serial.empty();
}

int exposureMilliseconds(float microseconds)
{
    return static_cast<int>(std::max(1.0f, std::floor(microseconds / 1000.0f + 0.5f)));
}

CameraControlRange floatControl(void* handle, const char* key, const char* label, bool exposure)
{
    CameraControlRange range;
    range.key = key;
    range.label = label;
    MVCC_FLOATVALUE value;
    std::memset(&value, 0, sizeof(value));
    if (handle && MV_CC_GetFloatValue(handle, key, &value) == MV_OK) {
        range.supported = true;
        range.writable = true;
        if (exposure) {
            range.min = std::min(100, std::max(1, static_cast<int>(std::ceil(value.fMin / 1000.0f))));
            range.max = std::max(range.min, std::min(100, static_cast<int>(std::floor(value.fMax / 1000.0f))));
            range.value = std::max(range.min, std::min(range.max, exposureMilliseconds(value.fCurValue)));
        } else {
            range.min = std::min(20, std::max(1, static_cast<int>(std::ceil(value.fMin))));
            range.max = std::max(range.min, std::min(20, static_cast<int>(std::floor(value.fMax))));
            range.value = std::max(range.min, std::min(range.max, static_cast<int>(std::floor(value.fCurValue + 0.5f))));
        }
        range.defaultValue = range.value;
        range.step = 1;
    }
    return range;
}

void setFloatControl(void* handle, const char* key, float requested)
{
    if (!handle) return;
    MVCC_FLOATVALUE range;
    std::memset(&range, 0, sizeof(range));
    if (MV_CC_GetFloatValue(handle, key, &range) == MV_OK) {
        requested = std::max(range.fMin, std::min(range.fMax, requested));
    }
    MV_CC_SetFloatValue(handle, key, requested);
}

CameraFrameData rgbFrame(void* handle, unsigned char* data, MV_FRAME_OUT_INFO_EX* info, uint64_t frameIndex)
{
    CameraFrameData frame;
    if (!handle || !data || !info || info->nWidth == 0 || info->nHeight == 0) return frame;
    frame.stream = "color";
    frame.format = "RGB8";
    frame.width = info->nWidth;
    frame.height = info->nHeight;
    frame.frameIndex = frameIndex;
    frame.capturedAt = nowText();
    const std::size_t pixelCount = static_cast<std::size_t>(frame.width) * frame.height;
    frame.data.resize(pixelCount * 3u);

    if (info->enPixelType == PixelType_Gvsp_Mono8 && info->nFrameLen >= pixelCount) {
        for (std::size_t i = 0; i < pixelCount; ++i) {
            frame.data[i * 3u] = data[i];
            frame.data[i * 3u + 1u] = data[i];
            frame.data[i * 3u + 2u] = data[i];
        }
        return frame;
    }

    MV_CC_PIXEL_CONVERT_PARAM_EX convert;
    std::memset(&convert, 0, sizeof(convert));
    convert.nWidth = info->nWidth;
    convert.nHeight = info->nHeight;
    convert.enSrcPixelType = info->enPixelType;
    convert.pSrcData = data;
    convert.nSrcDataLen = info->nFrameLen;
    convert.enDstPixelType = PixelType_Gvsp_RGB8_Packed;
    convert.pDstBuffer = frame.data.data();
    convert.nDstBufferSize = static_cast<unsigned int>(frame.data.size());
    if (MV_CC_ConvertPixelTypeEx(handle, &convert) != MV_OK) {
        return CameraFrameData();
    }
    return frame;
}

class HikvisionVendorCamera : public IVendorCamera {
public:
    HikvisionVendorCamera(void* handle, const DiscoveredCamera& info)
        : handle_(handle), info_(info), streaming_(false), triggerMode_(false), frameCount_(0)
    {
    }

    ~HikvisionVendorCamera()
    {
        stopStream();
        if (handle_) {
            MV_CC_CloseDevice(handle_);
            MV_CC_DestroyHandle(handle_);
            handle_ = 0;
        }
    }

    const std::string& vendor() const { return info_.vendor; }
    const std::string& name() const { return info_.name; }
    const std::string& serial() const { return info_.serial; }

    bool startStream(bool triggerMode)
    {
        std::lock_guard<std::mutex> lock(sdkMutex_);
        if (!handle_) return false;
        if (streaming_) return true;
        triggerMode_ = triggerMode;
        if (MV_CC_SetEnumValue(handle_, "TriggerMode", triggerMode ? 1 : 0) != MV_OK) return false;
        if (triggerMode && MV_CC_SetEnumValueByString(handle_, "TriggerSource", "Software") != MV_OK) return false;
        if (triggerMode) MV_CC_ClearImageBuffer(handle_);
        if (MV_CC_RegisterImageCallBackEx(handle_, &HikvisionVendorCamera::frameCallback, this) != MV_OK) return false;
        if (MV_CC_StartGrabbing(handle_) != MV_OK) return false;
        streaming_ = true;
        return true;
    }

    void stopStream()
    {
        std::lock_guard<std::mutex> lock(sdkMutex_);
        if (handle_ && streaming_) MV_CC_StopGrabbing(handle_);
        streaming_ = false;
    }

    bool streaming() const { return streaming_.load(); }

    CameraFrameData latestColorFrame() const
    {
        std::lock_guard<std::mutex> lock(frameMutex_);
        return latestColor_;
    }

    CameraControlState controls()
    {
        std::lock_guard<std::mutex> lock(sdkMutex_);
        CameraControlState state;
        if (!handle_) return state;
        MVCC_ENUMVALUE exposureAuto;
        std::memset(&exposureAuto, 0, sizeof(exposureAuto));
        if (MV_CC_GetEnumValue(handle_, "ExposureAuto", &exposureAuto) == MV_OK) {
            state.autoExposureSupported = true;
            state.autoExposureWritable = true;
            state.autoExposure = exposureAuto.nCurValue != 0;
        }
        state.exposure = floatControl(handle_, "ExposureTime", "曝光时间 (ms)", true);
        state.gain = floatControl(handle_, "Gain", "增益 (倍)", false);
        state.brightness.key = "brightness";
        state.brightness.label = "亮度";
        return state;
    }

    CameraControlState updateControls(const CameraControlUpdate& update)
    {
        {
            std::lock_guard<std::mutex> lock(sdkMutex_);
            if (handle_) {
                if (update.hasAutoExposure) MV_CC_SetEnumValue(handle_, "ExposureAuto", update.autoExposure ? 2 : 0);
                if (update.hasExposure) setFloatControl(handle_, "ExposureTime", static_cast<float>(std::max(1, std::min(100, update.exposure))) * 1000.0f);
                if (update.hasGain) setFloatControl(handle_, "Gain", static_cast<float>(std::max(1, std::min(20, update.gain))));
            }
        }
        return controls();
    }

    bool captureTriggerSet(VendorTriggerShot& out, TriggerBarrier& barrier, int timeoutMs, bool firstInBatch)
    {
        (void)firstInBatch;
        timeoutMs = timeoutMs > 0 ? timeoutMs : 1000;
        const bool restoreContinuous = !triggerMode_.load();
        struct Restorer {
            HikvisionVendorCamera* camera;
            bool restore;
            ~Restorer()
            {
                if (!restore) return;
                std::lock_guard<std::mutex> lock(camera->sdkMutex_);
                if (camera->handle_) MV_CC_SetEnumValue(camera->handle_, "TriggerMode", 0);
                camera->triggerMode_ = false;
            }
        } restorer = { this, restoreContinuous };

        bool ready = triggerMode_.load();
        if (!ready) {
            std::lock_guard<std::mutex> lock(sdkMutex_);
            const bool modeOk = handle_ && MV_CC_SetEnumValue(handle_, "TriggerMode", 1) == MV_OK;
            const bool sourceOk = handle_ && MV_CC_SetEnumValueByString(handle_, "TriggerSource", "Software") == MV_OK;
            ready = modeOk && sourceOk;
            if (ready) MV_CC_ClearImageBuffer(handle_);
            triggerMode_ = ready;
        }
        if (ready && restoreContinuous) std::this_thread::sleep_for(std::chrono::milliseconds(kTriggerModeSettleMs));

        barrier.arrive();
        if (!barrier.wait(timeoutMs) || !ready) {
            out.diagnostic = "Hikvision trigger barrier cancelled or trigger mode unavailable";
            out.ok = false;
            return false;
        }

        uint64_t baseline = 0;
        {
            std::lock_guard<std::mutex> lock(sdkMutex_);
            if (!handle_) {
                out.diagnostic = "Hikvision handle is unavailable";
                return false;
            }
            MV_CC_ClearImageBuffer(handle_);
            baseline = frameCount_.load();
            if (MV_CC_SetCommandValue(handle_, "TriggerSoftware") != MV_OK) {
                out.diagnostic = "Hikvision software trigger failed";
                return false;
            }
        }

        std::unique_lock<std::mutex> frameLock(frameMutex_);
        const bool received = frameCv_.wait_for(frameLock, std::chrono::milliseconds(timeoutMs), [this, baseline]() {
            return frameCount_.load() > baseline && latestColor_.valid();
        });
        if (received) out.color = latestColor_;
        out.ok = received && out.color.valid();
        if (!out.ok) out.diagnostic = "Timed out waiting for Hikvision color frame";
        return out.ok;
    }

private:
    void* handle_;
    DiscoveredCamera info_;
    mutable std::mutex sdkMutex_;
    mutable std::mutex frameMutex_;
    std::condition_variable frameCv_;
    std::atomic<bool> streaming_;
    std::atomic<bool> triggerMode_;
    std::atomic<uint64_t> frameCount_;
    CameraFrameData latestColor_;

    static void __stdcall frameCallback(unsigned char* data, MV_FRAME_OUT_INFO_EX* info, void* user)
    {
        if (user) static_cast<HikvisionVendorCamera*>(user)->onFrame(data, info);
    }

    void onFrame(unsigned char* data, MV_FRAME_OUT_INFO_EX* info)
    {
        try {
            const uint64_t index = frameCount_.load() + 1u;
            CameraFrameData frame = rgbFrame(handle_, data, info, index);
            if (!frame.valid()) return;
            {
                std::lock_guard<std::mutex> lock(frameMutex_);
                latestColor_ = frame;
                frameCount_ = index;
            }
            frameCv_.notify_all();
        } catch (...) {
        }
    }
};

} // namespace

HikvisionBackend::HikvisionBackend()
{
    MV_CC_Initialize();
}

HikvisionBackend::~HikvisionBackend() {}

std::vector<DiscoveredCamera> HikvisionBackend::discover(std::string& error)
{
    std::vector<DiscoveredCamera> result;
    MV_CC_DEVICE_INFO_LIST list;
    std::memset(&list, 0, sizeof(list));
    const int rc = MV_CC_EnumDevices(MV_USB_DEVICE | MV_GIGE_DEVICE, &list);
    if (rc != MV_OK) {
        error = "Failed to enumerate Hikvision cameras: " + std::to_string(rc);
        return result;
    }
    for (unsigned int i = 0; i < list.nDeviceNum; ++i) {
        DiscoveredCamera camera;
        if (discoveredFrom(list.pDeviceInfo[i], camera)) result.push_back(camera);
    }
    if (result.empty()) error = "Hikvision SDK found no Hikvision/Hikrobot cameras";
    return result;
}

std::shared_ptr<IVendorCamera> HikvisionBackend::open(const std::string& serial, std::string& error)
{
    MV_CC_DEVICE_INFO_LIST list;
    std::memset(&list, 0, sizeof(list));
    if (MV_CC_EnumDevices(MV_USB_DEVICE | MV_GIGE_DEVICE, &list) != MV_OK) {
        error = "Failed to enumerate Hikvision cameras";
        return std::shared_ptr<IVendorCamera>();
    }
    for (unsigned int i = 0; i < list.nDeviceNum; ++i) {
        DiscoveredCamera camera;
        if (!discoveredFrom(list.pDeviceInfo[i], camera) || camera.serial != serial) continue;
        void* handle = 0;
        if (MV_CC_CreateHandle(&handle, list.pDeviceInfo[i]) != MV_OK || !handle) {
            error = "Failed to create Hikvision handle: " + serial;
            return std::shared_ptr<IVendorCamera>();
        }
        const int rc = MV_CC_OpenDevice(handle, MV_ACCESS_Exclusive, 0);
        if (rc != MV_OK) {
            MV_CC_DestroyHandle(handle);
            error = "Failed to open Hikvision camera " + serial + ": " + std::to_string(rc);
            return std::shared_ptr<IVendorCamera>();
        }
        return std::shared_ptr<IVendorCamera>(new HikvisionVendorCamera(handle, camera));
    }
    error = "Hikvision camera not found: " + serial;
    return std::shared_ptr<IVendorCamera>();
}

} // namespace facescan
