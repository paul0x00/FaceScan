#include "vendor_camera.hpp"

#if defined(FACESCAN_WITH_HIKVISION)
#include "hikvision_camera.hpp"
#endif
#if defined(FACESCAN_WITH_MINDVISION)
#include "mindvision_camera.hpp"
#endif
#if defined(FACESCAN_WITH_ORBBEC)
#include "orbbec_multi_camera.hpp"
#endif

#include <chrono>

namespace facescan {

TriggerBarrier::TriggerBarrier()
    : expected_(0), arrived_(0), released_(false), cancelled_(false)
{
}

void TriggerBarrier::setExpected(int count)
{
    std::lock_guard<std::mutex> lock(mutex_);
    expected_ = count;
    arrived_ = 0;
    released_ = false;
    cancelled_ = false;
}

void TriggerBarrier::arrive()
{
    std::lock_guard<std::mutex> lock(mutex_);
    ++arrived_;
    cv_.notify_all();
}

bool TriggerBarrier::waitAllReady(int timeoutMs)
{
    std::unique_lock<std::mutex> lock(mutex_);
    cv_.wait_for(lock, std::chrono::milliseconds(timeoutMs), [this]() {
        return arrived_ >= expected_ || cancelled_;
    });
    return arrived_ >= expected_ && !cancelled_;
}

bool TriggerBarrier::wait(int timeoutMs)
{
    std::unique_lock<std::mutex> lock(mutex_);
    const bool ready = cv_.wait_for(lock, std::chrono::milliseconds(timeoutMs), [this]() {
        return released_;
    });
    return ready && !cancelled_;
}

void TriggerBarrier::release()
{
    std::lock_guard<std::mutex> lock(mutex_);
    released_ = true;
    cv_.notify_all();
}

void TriggerBarrier::cancelAndRelease()
{
    std::lock_guard<std::mutex> lock(mutex_);
    released_ = true;
    cancelled_ = true;
    cv_.notify_all();
}

IVendorCamera::~IVendorCamera() {}
IVendorCameraBackend::~IVendorCameraBackend() {}

std::vector<std::shared_ptr<IVendorCameraBackend>> createVendorCameraBackends()
{
    std::vector<std::shared_ptr<IVendorCameraBackend>> backends;
#if defined(FACESCAN_WITH_ORBBEC)
    backends.push_back(std::shared_ptr<IVendorCameraBackend>(new OrbbecMultiBackend()));
#endif
#if defined(FACESCAN_WITH_HIKVISION)
    backends.push_back(std::shared_ptr<IVendorCameraBackend>(new HikvisionBackend()));
#endif
#if defined(FACESCAN_WITH_MINDVISION)
    backends.push_back(std::shared_ptr<IVendorCameraBackend>(new MindvisionBackend()));
#endif
    return backends;
}

} // namespace facescan
