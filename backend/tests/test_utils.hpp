#pragma once

#include "common/file_utils.hpp"

#include <cstdlib>
#include <ctime>
#include <sstream>
#include <string>

namespace facescan_test {

/// 生成带测试名、时间戳和随机数的临时路径。
inline std::string makeTempPath(const std::string& name)
{
    const char* tmpDir = std::getenv("TMPDIR");
    const std::string root = tmpDir && tmpDir[0] ? tmpDir : "/tmp";
    std::ostringstream os;
    os << root << "/facescan_" << name << "_" << std::time(NULL) << "_" << std::rand();
    return os.str();
}

/// 测试用临时目录，生命周期结束时自动递归清理。
class ScopedTempDir {
public:
    /// 创建临时目录。
    explicit ScopedTempDir(const std::string& name)
        : path_(makeTempPath(name))
    {
        facescan::ensureDirectory(path_);
    }

    /// 清理临时目录。
    ~ScopedTempDir()
    {
        facescan::removePathRecursive(path_);
    }

    /// 返回临时目录路径。
    const std::string& path() const
    {
        return path_;
    }

private:
    /// 临时目录绝对路径。
    std::string path_;
};

} // namespace facescan_test
