#pragma once

#include <string>

namespace facescan {

/// 返回当前本地时间，格式为 yyyy-MM-dd HH:mm:ss。
std::string nowText();
/// 返回带毫秒的紧凑时间戳，用于生成文件名。
std::string stampTextMs();

} // namespace facescan
