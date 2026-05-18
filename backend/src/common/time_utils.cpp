#include "common/time_utils.hpp"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>

namespace facescan {

std::string nowText()
{
    std::time_t t = std::time(NULL);
    std::tm tmValue;
#if defined(_WIN32)
    localtime_s(&tmValue, &t);
#else
    localtime_r(&t, &tmValue);
#endif
    std::ostringstream os;
    os << std::put_time(&tmValue, "%Y-%m-%d %H:%M:%S");
    return os.str();
}

std::string stampTextMs()
{
    const std::chrono::system_clock::time_point now = std::chrono::system_clock::now();
    const std::time_t t = std::chrono::system_clock::to_time_t(now);
    const long ms = static_cast<long>(std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000);
    std::tm tmValue;
#if defined(_WIN32)
    localtime_s(&tmValue, &t);
#else
    localtime_r(&t, &tmValue);
#endif
    std::ostringstream os;
    os << std::put_time(&tmValue, "%Y%m%d%H%M%S") << std::setw(3) << std::setfill('0') << ms;
    return os.str();
}

} // namespace facescan
