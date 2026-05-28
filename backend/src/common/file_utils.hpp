#pragma once

#include <string>

namespace facescan {

/// 递归创建目录；目录已存在时视为成功。
void ensureDirectory(const std::string& path);
/// 返回路径的父目录，找不到分隔符时返回空字符串。
std::string parentDirectory(const std::string& path);
/// 判断路径是否为当前平台的绝对路径。
bool isAbsolutePath(const std::string& path);
/// 将相对路径拼接到 base；绝对路径会原样返回。
std::string joinPath(const std::string& base, const std::string& path);
/// 根据配置文件路径推导 backend 根目录。
std::string backendRootFromConfigPath(const std::string& configPath);
/// 读取文本文件内容；读取失败返回空字符串。
std::string readFileText(const std::string& path);
/// 使用系统文件管理器打开目录。
bool openDirectory(const std::string& path);
/// 判断路径是否存在。
bool pathExists(const std::string& path);
/// 递归复制文件或目录。
bool copyPathRecursive(const std::string& from, const std::string& to);
/// 优先重命名，失败时复制后删除源路径。
bool movePathRecursive(const std::string& from, const std::string& to);
/// 递归删除文件或目录，带基础路径保护。
bool removePathRecursive(const std::string& path);
/// 调用系统目录选择器并返回用户选择的目录。
std::string chooseDirectory();

} // namespace facescan
