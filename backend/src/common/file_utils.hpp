#pragma once

#include <string>

namespace facescan {

void ensureDirectory(const std::string& path);
std::string parentDirectory(const std::string& path);
bool isAbsolutePath(const std::string& path);
std::string joinPath(const std::string& base, const std::string& path);
std::string backendRootFromConfigPath(const std::string& configPath);
std::string readFileText(const std::string& path);
bool openDirectory(const std::string& path);
bool pathExists(const std::string& path);
bool copyPathRecursive(const std::string& from, const std::string& to);
bool movePathRecursive(const std::string& from, const std::string& to);
bool removePathRecursive(const std::string& path);
std::string chooseDirectory();

} // namespace facescan
