#include "multi_camera_device.hpp"

#include "vendor_camera.hpp"
#include "../common/bmp_utils.hpp"
#include "../common/file_utils.hpp"
#include "../common/json_utils.hpp"
#include "../common/time_utils.hpp"

#include <algorithm>
#include <atomic>
#include <fstream>
#include <map>
#include <mutex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <vector>

namespace facescan {
namespace {

const char* kPrimaryViews[] = { "left", "front", "right", "bottom" };

std::string bmpFromRgb(const CameraFrameData& frame)
{
    if (!frame.valid()) return "";
    const std::vector<unsigned char> bytes = encodeBmpRgb(frame.width, frame.height, frame.data);
    return std::string(bytes.begin(), bytes.end());
}

void writeRgbBmp(const std::string& path, const CameraFrameData& frame)
{
    if (!frame.valid() || !writeBmpRgb(path, frame.width, frame.height, frame.data)) {
        throw std::runtime_error("Cannot write RGB BMP frame: " + path);
    }
}

void writeGrayBmp(const std::string& path, const CameraFrameData& frame)
{
    if (!frame.valid() || !writeBmpGray(path, frame.width, frame.height, frame.data)) {
        throw std::runtime_error("Cannot write grayscale BMP frame: " + path);
    }
}

std::string escapeXml(const std::string& value)
{
    std::string escaped;
    escaped.reserve(value.size());
    for (std::size_t i = 0; i < value.size(); ++i) {
        switch (value[i]) {
        case '&': escaped += "&amp;"; break;
        case '<': escaped += "&lt;"; break;
        case '>': escaped += "&gt;"; break;
        case '"': escaped += "&quot;"; break;
        case '\'': escaped += "&apos;"; break;
        default: escaped.push_back(value[i]); break;
        }
    }
    return escaped;
}

std::string waitingSvg(const std::string& label, const std::string& detail)
{
    std::ostringstream os;
    os << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"640\" height=\"400\" viewBox=\"0 0 640 400\">";
    os << "<rect width=\"640\" height=\"400\" fill=\"#eef3f5\"/>";
    os << "<rect x=\"24\" y=\"24\" width=\"592\" height=\"352\" rx=\"10\" fill=\"none\" stroke=\"#9fb0bb\" stroke-width=\"3\"/>";
    os << "<text x=\"320\" y=\"190\" text-anchor=\"middle\" font-family=\"Arial,sans-serif\" font-size=\"22\" font-weight=\"700\" fill=\"#3f5663\">" << escapeXml(label) << "</text>";
    os << "<text x=\"320\" y=\"226\" text-anchor=\"middle\" font-family=\"Arial,sans-serif\" font-size=\"14\" fill=\"#6b7d87\">" << escapeXml(detail) << "</text>";
    os << "</svg>";
    return os.str();
}

std::shared_ptr<IVendorCameraBackend> backendByVendor(
    const std::vector<std::shared_ptr<IVendorCameraBackend>>& backends,
    const std::string& vendor)
{
    for (std::size_t i = 0; i < backends.size(); ++i) {
        if (vendor == backends[i]->vendor()) {
            return backends[i];
        }
    }
    return std::shared_ptr<IVendorCameraBackend>();
}

std::string chooseSerial(
    const std::vector<DiscoveredCamera>& devices,
    const std::string& configured,
    std::set<std::string>& used)
{
    if (!configured.empty()) {
        for (std::size_t i = 0; i < devices.size(); ++i) {
            if (devices[i].serial == configured && used.insert(configured).second) {
                return configured;
            }
        }
    }
    for (std::size_t i = 0; i < devices.size(); ++i) {
        if (used.insert(devices[i].serial).second) {
            return devices[i].serial;
        }
    }
    return "";
}

struct NamedShot {
    std::string role;
    std::shared_ptr<IVendorCamera> device;
    VendorTriggerShot shot;
};

class MultiCameraDevice : public ICameraDevice {
public:
    MultiCameraDevice(const std::string& imageRoot, const MultiCameraConfig& config)
        : imageRoot_(CameraManager::normalizeRoot(imageRoot)), config_(config), streaming_(false), initialized_(false), controlsInitialized_(false)
    {
        if (config_.triggerWorkflow != "sequential_modules" &&
            config_.triggerWorkflow != "orbbec_parallel_then_module4") {
            config_.triggerWorkflow = "orbbec_parallel_then_module4";
        }
        if (config_.triggerTimeoutMs <= 0) config_.triggerTimeoutMs = 1000;
    }

    ~MultiCameraDevice()
    {
        try {
            stop();
        } catch (...) {
        }
    }

    void start()
    {
        std::lock_guard<std::mutex> operation(operationMutex_);
        ensureInitialized();
        std::vector<std::shared_ptr<IVendorCamera>> devices = allDevices();
        std::vector<std::shared_ptr<IVendorCamera>> started;
        for (std::size_t i = 0; i < devices.size(); ++i) {
            if (!devices[i]->startStream(false)) {
                for (std::size_t j = 0; j < started.size(); ++j) {
                    started[j]->stopStream();
                }
                throw std::runtime_error("Failed to start " + devices[i]->vendor() + " camera " + devices[i]->serial());
            }
            started.push_back(devices[i]);
        }
        streaming_ = true;
    }

    void stop()
    {
        std::lock_guard<std::mutex> operation(operationMutex_);
        std::vector<std::shared_ptr<IVendorCamera>> devices = allDevices();
        for (std::size_t i = 0; i < devices.size(); ++i) {
            devices[i]->stopStream();
        }
        streaming_ = false;
    }

    bool streaming() const { return streaming_; }

    void setImageRoot(const std::string& imageRoot)
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        imageRoot_ = CameraManager::normalizeRoot(imageRoot);
    }

    CameraControlState controls()
    {
        std::lock_guard<std::mutex> operation(operationMutex_);
        ensureInitialized();
        ensureUnifiedControls();
        return controlState_;
    }

    CameraControlState updateControls(const CameraControlUpdate& update)
    {
        std::lock_guard<std::mutex> operation(operationMutex_);
        ensureInitialized();
        ensureUnifiedControls();

        CameraControlUpdate normalized = update;
        if (normalized.hasExposure) normalized.exposure = std::max(1, std::min(100, normalized.exposure));
        if (normalized.hasGain) normalized.gain = std::max(1, std::min(20, normalized.gain));

        const std::vector<std::shared_ptr<IVendorCamera>> devices = allDevices();
        for (std::size_t i = 0; i < devices.size(); ++i) {
            devices[i]->updateControls(normalized);
        }
        if (normalized.hasAutoExposure) controlState_.autoExposure = normalized.autoExposure;
        if (normalized.hasExposure) controlState_.exposure.value = normalized.exposure;
        if (normalized.hasGain) controlState_.gain.value = normalized.gain;
        return controlState_;
    }

    CameraImage frameImage(const std::string& view)
    {
        std::shared_ptr<IVendorCamera> device;
        std::string label;
        std::string initializationDetail;
        {
            std::lock_guard<std::mutex> lock(stateMutex_);
            initializationDetail = initializationError_;
            if (view == "left") {
                device = left_;
                label = "左侧 Orbbec 彩色图";
            } else if (view == "right") {
                device = right_;
                label = "右侧 Orbbec 彩色图";
            } else if (view == "bottom") {
                device = bottom_;
                label = "下方 Orbbec 彩色图";
            } else {
                device = front_;
                label = "正面海康彩色图";
            }
        }
        if (!device) {
            return CameraImage(waitingSvg(label, initializationDetail.empty() ? "等待相机启动" : initializationDetail), "image/svg+xml; charset=utf-8");
        }
        const CameraFrameData frame = device->latestColorFrame();
        const std::string bmp = bmpFromRgb(frame);
        if (bmp.empty()) {
            return CameraImage(waitingSvg(label, "等待实时彩色帧"), "image/svg+xml; charset=utf-8");
        }
        return CameraImage(bmp, "image/bmp");
    }

    std::vector<std::string> capture(const std::string& orderFolder, const std::string& orderName)
    {
        std::lock_guard<std::mutex> operation(operationMutex_);
        ensureInitialized();
        if (!streaming_) {
            throw std::runtime_error("Multi-camera preview is not running");
        }

        std::vector<NamedShot> results;
        const bool parallelOrbbec = config_.triggerWorkflow != "sequential_modules";
        if (parallelOrbbec) {
            std::vector<NamedShot> group;
            group.push_back(makeNamedShot("left", left_));
            group.push_back(makeNamedShot("right", right_));
            group.push_back(makeNamedShot("bottom", bottom_));
            captureGroup(group);
            results.insert(results.end(), group.begin(), group.end());
        } else {
            const char* roles[] = { "left", "right", "bottom" };
            std::shared_ptr<IVendorCamera> devices[] = { left_, right_, bottom_ };
            for (int i = 0; i < 3; ++i) {
                std::vector<NamedShot> group(1, makeNamedShot(roles[i], devices[i]));
                captureGroup(group);
                results.push_back(group[0]);
            }
        }

        std::vector<NamedShot> module4;
        module4.push_back(makeNamedShot("front", front_));
        module4.push_back(makeNamedShot("stereo", stereo_));
        captureGroup(module4);
        results.insert(results.end(), module4.begin(), module4.end());

        return saveCapture(orderFolder, orderName, results);
    }

private:
    std::string imageRoot_;
    MultiCameraConfig config_;
    mutable std::mutex stateMutex_;
    std::mutex operationMutex_;
    std::atomic<bool> streaming_;
    bool initialized_;
    bool controlsInitialized_;
    CameraControlState controlState_;
    std::string initializationError_;
    std::vector<std::shared_ptr<IVendorCameraBackend>> backends_;
    std::shared_ptr<IVendorCamera> left_;
    std::shared_ptr<IVendorCamera> right_;
    std::shared_ptr<IVendorCamera> bottom_;
    std::shared_ptr<IVendorCamera> front_;
    std::shared_ptr<IVendorCamera> stereo_;

    void ensureUnifiedControls()
    {
        if (controlsInitialized_) return;
        const CameraControlState seed = front_->controls();
        controlState_.autoExposureSupported = true;
        controlState_.autoExposureWritable = true;
        controlState_.autoExposure = seed.autoExposureSupported ? seed.autoExposure : false;

        controlState_.exposure.key = "exposure";
        controlState_.exposure.label = "曝光时间 (ms)";
        controlState_.exposure.supported = true;
        controlState_.exposure.writable = true;
        controlState_.exposure.min = 1;
        controlState_.exposure.max = 100;
        controlState_.exposure.step = 1;
        controlState_.exposure.value = std::max(1, std::min(100, seed.exposure.value > 0 ? seed.exposure.value : 20));
        controlState_.exposure.defaultValue = controlState_.exposure.value;

        controlState_.gain.key = "gain";
        controlState_.gain.label = "增益 (倍)";
        controlState_.gain.supported = true;
        controlState_.gain.writable = true;
        controlState_.gain.min = 1;
        controlState_.gain.max = 20;
        controlState_.gain.step = 1;
        controlState_.gain.value = std::max(1, std::min(20, seed.gain.value > 0 ? seed.gain.value : 1));
        controlState_.gain.defaultValue = controlState_.gain.value;

        controlState_.brightness.key = "brightness";
        controlState_.brightness.label = "亮度";
        controlsInitialized_ = true;
    }

    static NamedShot makeNamedShot(const std::string& role, const std::shared_ptr<IVendorCamera>& device)
    {
        NamedShot result;
        result.role = role;
        result.device = device;
        return result;
    }

    std::vector<std::shared_ptr<IVendorCamera>> allDevices() const
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        std::vector<std::shared_ptr<IVendorCamera>> devices;
        if (left_) devices.push_back(left_);
        if (right_) devices.push_back(right_);
        if (bottom_) devices.push_back(bottom_);
        if (front_) devices.push_back(front_);
        if (stereo_) devices.push_back(stereo_);
        return devices;
    }

    void ensureInitialized()
    {
        {
            std::lock_guard<std::mutex> lock(stateMutex_);
            if (initialized_) {
                return;
            }
        }
        try {
            initializeDevices();
            std::lock_guard<std::mutex> lock(stateMutex_);
            initialized_ = true;
            initializationError_.clear();
        } catch (const std::exception& e) {
            std::lock_guard<std::mutex> lock(stateMutex_);
            initializationError_ = e.what();
            throw;
        }
    }

    void initializeDevices()
    {
        backends_ = createVendorCameraBackends();
        const std::shared_ptr<IVendorCameraBackend> orbbec = backendByVendor(backends_, "Orbbec");
        const std::shared_ptr<IVendorCameraBackend> hikvision = backendByVendor(backends_, "Hikvision");
        const std::shared_ptr<IVendorCameraBackend> mindvision = backendByVendor(backends_, "MindVision");
        if (!orbbec || !hikvision || !mindvision) {
            throw std::runtime_error("multi_camera mode requires Orbbec, Hikvision and MindVision SDK support in this build");
        }

        std::string error;
        const std::vector<DiscoveredCamera> orbbecDevices = orbbec->discover(error);
        if (orbbecDevices.size() < 3u) {
            throw std::runtime_error(error.empty() ? "Expected three Orbbec cameras" : error);
        }
        const std::vector<DiscoveredCamera> hikvisionDevices = hikvision->discover(error);
        if (hikvisionDevices.empty()) {
            throw std::runtime_error(error.empty() ? "Expected one Hikvision camera" : error);
        }
        const std::vector<DiscoveredCamera> mindvisionDevices = mindvision->discover(error);
        if (mindvisionDevices.empty()) {
            throw std::runtime_error(error.empty() ? "Expected one MindVision stereo camera" : error);
        }

        std::set<std::string> usedOrbbec;
        const std::string leftSerial = chooseSerial(orbbecDevices, config_.orbbecLeftSerial, usedOrbbec);
        const std::string rightSerial = chooseSerial(orbbecDevices, config_.orbbecRightSerial, usedOrbbec);
        const std::string bottomSerial = chooseSerial(orbbecDevices, config_.orbbecBottomSerial, usedOrbbec);
        std::set<std::string> usedSingle;
        const std::string frontSerial = chooseSerial(hikvisionDevices, config_.hikvisionFrontSerial, usedSingle);
        usedSingle.clear();
        const std::string stereoSerial = chooseSerial(mindvisionDevices, config_.mindvisionStereoSerial, usedSingle);

        std::shared_ptr<IVendorCamera> left = openRequired(orbbec, leftSerial, "left Orbbec");
        std::shared_ptr<IVendorCamera> right = openRequired(orbbec, rightSerial, "right Orbbec");
        std::shared_ptr<IVendorCamera> bottom = openRequired(orbbec, bottomSerial, "bottom Orbbec");
        std::shared_ptr<IVendorCamera> front = openRequired(hikvision, frontSerial, "front Hikvision");
        std::shared_ptr<IVendorCamera> stereo = openRequired(mindvision, stereoSerial, "MindVision stereo");

        std::lock_guard<std::mutex> lock(stateMutex_);
        left_ = left;
        right_ = right;
        bottom_ = bottom;
        front_ = front;
        stereo_ = stereo;
    }

    static std::shared_ptr<IVendorCamera> openRequired(
        const std::shared_ptr<IVendorCameraBackend>& backend,
        const std::string& serial,
        const std::string& role)
    {
        if (serial.empty()) {
            throw std::runtime_error("No device available for " + role);
        }
        std::string error;
        std::shared_ptr<IVendorCamera> device = backend->open(serial, error);
        if (!device) {
            throw std::runtime_error(error.empty() ? "Cannot open " + role + " camera " + serial : error);
        }
        return device;
    }

    void captureGroup(std::vector<NamedShot>& group)
    {
        TriggerBarrier barrier;
        barrier.setExpected(static_cast<int>(group.size()));
        std::vector<std::thread> workers;
        workers.reserve(group.size());
        for (std::size_t i = 0; i < group.size(); ++i) {
            workers.push_back(std::thread([this, &barrier, &group, i]() {
                try {
                    group[i].device->captureTriggerSet(group[i].shot, barrier, config_.triggerTimeoutMs, true);
                } catch (const std::exception& e) {
                    group[i].shot.ok = false;
                    group[i].shot.diagnostic = e.what();
                    barrier.cancelAndRelease();
                } catch (...) {
                    group[i].shot.ok = false;
                    group[i].shot.diagnostic = "Unknown camera capture error";
                    barrier.cancelAndRelease();
                }
            }));
        }
        if (barrier.waitAllReady(config_.triggerTimeoutMs)) {
            barrier.release();
        } else {
            barrier.cancelAndRelease();
        }
        for (std::size_t i = 0; i < workers.size(); ++i) {
            workers[i].join();
        }
    }

    std::vector<std::string> saveCapture(
        const std::string& orderFolder,
        const std::string& orderName,
        const std::vector<NamedShot>& results)
    {
        ensureDirectory(orderFolder);
        const std::string frontFolder = orderFolder + "/front";
        const std::string leftFolder = orderFolder + "/left";
        const std::string rightFolder = orderFolder + "/right";
        const std::string bottomFolder = orderFolder + "/bottom";
        ensureDirectory(frontFolder);
        ensureDirectory(leftFolder);
        ensureDirectory(rightFolder);
        ensureDirectory(bottomFolder);

        std::map<std::string, std::string> primaryPaths;
        std::vector<std::string> rawPaths;
        std::ostringstream manifest;
        manifest << "{\n  \"workflow\": \"" << escapeJson(config_.triggerWorkflow)
                 << "\",\n  \"capturedAt\": \"" << escapeJson(nowText())
                 << "\",\n  \"devices\": [\n";
        for (std::size_t i = 0; i < results.size(); ++i) {
            const NamedShot& result = results[i];
            const std::string vendor = result.device->vendor();
            manifest << "    {\"role\":\"" << escapeJson(result.role) << "\",\"vendor\":\""
                     << escapeJson(vendor) << "\",\"serial\":\"" << escapeJson(result.device->serial())
                     << "\",\"ok\":" << (result.shot.ok ? "true" : "false")
                     << ",\"diagnostic\":\"" << escapeJson(result.shot.diagnostic) << "\"}"
                     << (i + 1u == results.size() ? "\n" : ",\n");

            if (vendor == "Orbbec") {
                const std::string moduleFolder = orderFolder + "/" + result.role;
                if (result.shot.color.valid()) {
                    const std::string path = moduleFolder + "/" + orderName + "_color.bmp";
                    writeRgbBmp(path, result.shot.color);
                    rawPaths.push_back(path);
                    if (result.role == "left" || result.role == "right" || result.role == "bottom") {
                        primaryPaths[result.role] = path;
                    }
                }
                if (result.shot.left.valid()) {
                    const std::string path = moduleFolder + "/" + orderName + "_left_ir.bmp";
                    writeGrayBmp(path, result.shot.left);
                    rawPaths.push_back(path);
                }
                if (result.shot.right.valid()) {
                    const std::string path = moduleFolder + "/" + orderName + "_right_ir.bmp";
                    writeGrayBmp(path, result.shot.right);
                    rawPaths.push_back(path);
                }
            } else if (vendor == "Hikvision" && result.role == "front") {
                if (result.shot.color.valid()) {
                    const std::string path = frontFolder + "/" + orderName + "_color.bmp";
                    writeRgbBmp(path, result.shot.color);
                    rawPaths.push_back(path);
                    primaryPaths["front"] = path;
                }
            } else if (vendor == "MindVision") {
                if (result.shot.left.valid()) {
                    const std::string path = frontFolder + "/" + orderName + "_left.bmp";
                    writeGrayBmp(path, result.shot.left);
                    rawPaths.push_back(path);
                }
                if (result.shot.right.valid()) {
                    const std::string path = frontFolder + "/" + orderName + "_right.bmp";
                    writeGrayBmp(path, result.shot.right);
                    rawPaths.push_back(path);
                }
            }
        }
        manifest << "  ],\n  \"rawPaths\": [";
        for (std::size_t i = 0; i < rawPaths.size(); ++i) {
            if (i) manifest << ',';
            manifest << "\"" << escapeJson(rawPaths[i]) << "\"";
        }
        manifest << "]\n}\n";
        const std::string manifestPath = orderFolder + "/" + orderName + "_multi_camera_capture.json";
        std::ofstream manifestFile(manifestPath.c_str(), std::ios::binary | std::ios::trunc);
        if (!manifestFile) {
            throw std::runtime_error("Cannot write multi-camera capture manifest: " + manifestPath);
        }
        manifestFile << manifest.str();
        if (!manifestFile) {
            throw std::runtime_error("Failed to write multi-camera capture manifest: " + manifestPath);
        }
        manifestFile.close();

        for (std::size_t i = 0; i < results.size(); ++i) {
            if (!results[i].shot.ok) {
                throw std::runtime_error(
                    "Incomplete trigger set for " + results[i].role + "; see " + manifestPath);
            }
        }

        std::vector<std::string> orderedPaths;
        for (std::size_t i = 0; i < sizeof(kPrimaryViews) / sizeof(kPrimaryViews[0]); ++i) {
            const std::string view = kPrimaryViews[i];
            const std::map<std::string, std::string>::const_iterator found = primaryPaths.find(view);
            if (found == primaryPaths.end()) {
                throw std::runtime_error("Missing primary color frame for view: " + view + "; see " + manifestPath);
            }
            orderedPaths.push_back(found->second);
        }
        for (std::size_t i = 0; i < rawPaths.size(); ++i) {
            if (std::find(orderedPaths.begin(), orderedPaths.end(), rawPaths[i]) == orderedPaths.end()) {
                orderedPaths.push_back(rawPaths[i]);
            }
        }
        return orderedPaths;
    }
};

} // namespace

std::unique_ptr<ICameraDevice> createMultiCameraDevice(
    const std::string& imageRoot,
    const MultiCameraConfig& config)
{
    return std::unique_ptr<ICameraDevice>(new MultiCameraDevice(imageRoot, config));
}

} // namespace facescan
