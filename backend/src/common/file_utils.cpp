#include "file_utils.hpp"

#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>

#if defined(_WIN32)
#include <direct.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

namespace facescan {

namespace {

/// 创建单级目录，跨平台兼容“已存在”场景。
bool makeDirectory(const std::string& path)
{
    if (path.empty()) {
        return true;
    }
#if defined(_WIN32)
    if (_mkdir(path.c_str()) == 0 || errno == EEXIST) {
        return true;
    }
#else
    if (mkdir(path.c_str(), 0755) == 0 || errno == EEXIST) {
        return true;
    }
#endif
    return false;
}

/// 将路径安全包装为系统 shell 命令参数。
std::string shellQuote(const std::string& path)
{
#if defined(_WIN32)
    std::string out = "\"";
    for (std::string::const_iterator it = path.begin(); it != path.end(); ++it) {
        if (*it == '"') {
            out += "\\\"";
        } else {
            out.push_back(*it);
        }
    }
    out += "\"";
    return out;
#else
    std::string out = "'";
    for (std::string::const_iterator it = path.begin(); it != path.end(); ++it) {
        if (*it == '\'') {
            out += "'\\''";
        } else {
            out.push_back(*it);
        }
    }
    out += "'";
    return out;
#endif
}

} // namespace

/// 递归创建目录。
void ensureDirectory(const std::string& path)
{
    std::string current;
    for (std::size_t i = 0; i < path.size(); ++i) {
        current.push_back(path[i]);
        if (path[i] == '/' || i + 1 == path.size()) {
            if (current.empty() || current == "/") {
                continue;
            }
            makeDirectory(current);
        }
    }
}

/// 返回父目录路径。
std::string parentDirectory(const std::string& path)
{
    const std::size_t pos = path.find_last_of("/\\");
    if (pos == std::string::npos) {
        return "";
    }
    return path.substr(0, pos);
}

/// 判断路径是否为绝对路径。
bool isAbsolutePath(const std::string& path)
{
    if (path.empty()) {
        return false;
    }
#if defined(_WIN32)
    return path.size() > 2 && std::isalpha(static_cast<unsigned char>(path[0])) && path[1] == ':';
#else
    return path[0] == '/';
#endif
}

/// 拼接 base 和相对路径。
std::string joinPath(const std::string& base, const std::string& path)
{
    if (path.empty() || isAbsolutePath(path) || base.empty() || base == ".") {
        return path;
    }
    const char last = base[base.size() - 1];
    if (last == '/' || last == '\\') {
        return base + path;
    }
    return base + "/" + path;
}

/// 从 config/app.json 推导 backend 根目录。
std::string backendRootFromConfigPath(const std::string& configPath)
{
    const std::string configDir = parentDirectory(configPath);
    const std::string root = parentDirectory(configDir);
    return root.empty() ? "." : root;
}

/// 读取完整文本文件。
std::string readFileText(const std::string& path)
{
    std::ifstream file(path.c_str(), std::ios::binary);
    if (!file) {
        return "";
    }
    std::ostringstream os;
    os << file.rdbuf();
    return os.str();
}

/// 调用系统文件管理器打开目录。
bool openDirectory(const std::string& path)
{
    if (path.empty()) {
        return false;
    }
#if defined(_WIN32)
    const std::string command = "start \"\" " + shellQuote(path);
#elif defined(__APPLE__)
    const std::string command = "open " + shellQuote(path);
#else
    const std::string command = "xdg-open " + shellQuote(path);
#endif
    return std::system(command.c_str()) == 0;
}

/// 判断路径是否存在。
bool pathExists(const std::string& path)
{
    if (path.empty()) {
        return false;
    }
#if defined(_WIN32)
    struct _stat info;
    return _stat(path.c_str(), &info) == 0;
#else
    struct stat info;
    return lstat(path.c_str(), &info) == 0;
#endif
}

/// 递归复制文件或目录。
bool copyPathRecursive(const std::string& from, const std::string& to)
{
    if (from.empty() || to.empty() || from == to) {
        return false;
    }
#if defined(_WIN32)
    struct _stat info;
    if (_stat(from.c_str(), &info) != 0) {
        return false;
    }
    if (!(info.st_mode & _S_IFDIR)) {
        ensureDirectory(parentDirectory(to));
        std::ifstream input(from.c_str(), std::ios::binary);
        std::ofstream output(to.c_str(), std::ios::binary | std::ios::trunc);
        output << input.rdbuf();
        return input.good() && output.good();
    }
    ensureDirectory(to);
    const std::string command = "xcopy " + shellQuote(from) + " " + shellQuote(to) + " /E /I /Y >NUL";
    return std::system(command.c_str()) == 0;
#else
    struct stat info;
    if (lstat(from.c_str(), &info) != 0) {
        return false;
    }
    if (!S_ISDIR(info.st_mode)) {
        ensureDirectory(parentDirectory(to));
        std::ifstream input(from.c_str(), std::ios::binary);
        std::ofstream output(to.c_str(), std::ios::binary | std::ios::trunc);
        output << input.rdbuf();
        return input.good() && output.good();
    }

    ensureDirectory(to);
    DIR* dir = opendir(from.c_str());
    if (!dir) {
        return false;
    }
    bool ok = true;
    while (dirent* entry = readdir(dir)) {
        const std::string name = entry->d_name;
        if (name == "." || name == "..") {
            continue;
        }
        if (!copyPathRecursive(from + "/" + name, to + "/" + name)) {
            ok = false;
        }
    }
    closedir(dir);
    return ok;
#endif
}

/// 移动文件或目录；跨卷移动时退化为复制后删除。
bool movePathRecursive(const std::string& from, const std::string& to)
{
    if (from.empty() || to.empty() || from == to || !pathExists(from)) {
        return true;
    }
    ensureDirectory(parentDirectory(to));
    if (std::rename(from.c_str(), to.c_str()) == 0) {
        return true;
    }
    if (!copyPathRecursive(from, to)) {
        return false;
    }
    return removePathRecursive(from);
}

/// 递归删除文件或目录。
bool removePathRecursive(const std::string& path)
{
    if (path.empty() || path == "/" || path == "." || path == "..") {
        return false;
    }
#if defined(_WIN32)
    struct _stat info;
    if (_stat(path.c_str(), &info) != 0) {
        return true;
    }
    const std::string command = "rmdir /S /Q " + shellQuote(path);
    return std::system(command.c_str()) == 0 || std::remove(path.c_str()) == 0;
#else
    struct stat info;
    if (lstat(path.c_str(), &info) != 0) {
        return errno == ENOENT;
    }
    if (!S_ISDIR(info.st_mode)) {
        return unlink(path.c_str()) == 0 || errno == ENOENT;
    }

    DIR* dir = opendir(path.c_str());
    if (!dir) {
        return false;
    }
    bool ok = true;
    while (dirent* entry = readdir(dir)) {
        const std::string name = entry->d_name;
        if (name == "." || name == "..") {
            continue;
        }
        const std::string child = path + "/" + name;
        if (!removePathRecursive(child)) {
            ok = false;
        }
    }
    closedir(dir);
    return (rmdir(path.c_str()) == 0 || errno == ENOENT) && ok;
#endif
}

/// 打开平台目录选择器。
std::string chooseDirectory()
{
#if defined(_WIN32)
    const char* command = "powershell -NoProfile -Command \"Add-Type -AssemblyName System.Windows.Forms; $d=New-Object System.Windows.Forms.FolderBrowserDialog; if ($d.ShowDialog() -eq 'OK') { $d.SelectedPath }\"";
#elif defined(__APPLE__)
    const char* command = "osascript -e 'POSIX path of (choose folder with prompt \"选择数据保存目录\")'";
#else
    const char* command = "if command -v zenity >/dev/null 2>&1; then zenity --file-selection --directory --title='选择数据保存目录'; elif command -v kdialog >/dev/null 2>&1; then kdialog --getexistingdirectory .; fi";
#endif
#if defined(_WIN32)
    FILE* pipe = _popen(command, "r");
#else
    FILE* pipe = popen(command, "r");
#endif
    if (!pipe) {
        return "";
    }
    char buffer[512];
    std::string output;
    while (fgets(buffer, sizeof(buffer), pipe)) {
        output += buffer;
    }
#if defined(_WIN32)
    _pclose(pipe);
#else
    pclose(pipe);
#endif
    while (!output.empty() && (output[output.size() - 1] == '\n' || output[output.size() - 1] == '\r')) {
        output.erase(output.size() - 1);
    }
    if (!output.empty() && (output[output.size() - 1] == '/' || output[output.size() - 1] == '\\')) {
        output.erase(output.size() - 1);
    }
    return output;
}

} // namespace facescan
