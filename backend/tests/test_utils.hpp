#pragma once

#include "common/file_utils.hpp"

#include <cstdlib>
#include <ctime>
#include <sstream>
#include <string>

namespace facescan_test {

inline std::string makeTempPath(const std::string& name)
{
    const char* tmpDir = std::getenv("TMPDIR");
    const std::string root = tmpDir && tmpDir[0] ? tmpDir : "/tmp";
    std::ostringstream os;
    os << root << "/facescan_" << name << "_" << std::time(NULL) << "_" << std::rand();
    return os.str();
}

class ScopedTempDir {
public:
    explicit ScopedTempDir(const std::string& name)
        : path_(makeTempPath(name))
    {
        facescan::ensureDirectory(path_);
    }

    ~ScopedTempDir()
    {
        facescan::removePathRecursive(path_);
    }

    const std::string& path() const
    {
        return path_;
    }

private:
    std::string path_;
};

} // namespace facescan_test
