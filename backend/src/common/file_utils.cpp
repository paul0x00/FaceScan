#include "common/file_utils.hpp"

#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <fstream>
#include <sstream>

#if defined(_WIN32)
#include <direct.h>
#else
#include <sys/stat.h>
#include <sys/types.h>
#endif

namespace facescan {

namespace {

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

std::string parentDirectory(const std::string& path)
{
    const std::size_t pos = path.find_last_of("/\\");
    if (pos == std::string::npos) {
        return "";
    }
    return path.substr(0, pos);
}

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

std::string backendRootFromConfigPath(const std::string& configPath)
{
    const std::string configDir = parentDirectory(configPath);
    const std::string root = parentDirectory(configDir);
    return root.empty() ? "." : root;
}

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

} // namespace facescan
