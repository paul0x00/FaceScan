#include "app.hpp"

#include "algorithm/color_point_cloud_sdk.hpp"
#include "camera/camera_manager.hpp"
#include "common/file_utils.hpp"
#include "common/json_utils.hpp"
#include "common/models.hpp"
#include "common/time_utils.hpp"
#include "database/database.hpp"

#include <boost/asio.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <zlib.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#if defined(_WIN32)
#include <direct.h>
#include <windows.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace beast = boost::beast;
namespace http = beast::http;
namespace asio = boost::asio;
using tcp = asio::ip::tcp;

namespace facescan {

namespace {

std::string toString(int value)
{
    std::ostringstream os;
    os << value;
    return os.str();
}

std::string toJsonNumber(double value)
{
    std::ostringstream os;
    os << value;
    return os.str();
}

std::string safePathSegment(const std::string& value)
{
    std::string out;
    for (std::string::const_iterator it = value.begin(); it != value.end(); ++it) {
        const unsigned char c = static_cast<unsigned char>(*it);
        if (c < 32 || *it == '/' || *it == '\\' || *it == ':' || *it == '*' || *it == '?' ||
            *it == '"' || *it == '<' || *it == '>' || *it == '|') {
            out.push_back('_');
        } else {
            out.push_back(*it);
        }
    }
    while (!out.empty() && (out[out.size() - 1] == ' ' || out[out.size() - 1] == '.')) {
        out.erase(out.size() - 1);
    }
    return out.empty() ? "unnamed" : out;
}

std::string patientFolderName(const Patient& patient)
{
    const std::string patientNo = patient.patientNo.empty() ? ("patient_" + toString(patient.id)) : patient.patientNo;
    return safePathSegment(patientNo + "-" + patient.name);
}

std::string legacyPatientFolderName(const Patient& patient)
{
    return safePathSegment(toString(patient.id) + "_" + patient.name);
}

std::string patientDirectory(const std::string& imageRoot, const Patient& patient)
{
    return imageRoot + "/" + patientFolderName(patient);
}

std::string legacyPatientDirectory(const std::string& imageRoot, const Patient& patient)
{
    return imageRoot + "/" + legacyPatientFolderName(patient);
}

std::string existingPatientDirectory(const std::string& imageRoot, const Patient& patient)
{
    const std::string current = patientDirectory(imageRoot, patient);
    if (pathExists(current)) {
        return current;
    }
    const std::string legacy = legacyPatientDirectory(imageRoot, patient);
    if (pathExists(legacy)) {
        return legacy;
    }
    return current;
}

std::string orderFolderName(const std::string& orderNo)
{
    return safePathSegment(orderNo.empty() ? "order" : orderNo);
}

std::string orderDirectory(const std::string& imageRoot, const Patient& patient, const std::string& orderNo)
{
    return patientDirectory(imageRoot, patient) + "/" + orderFolderName(orderNo);
}

bool isNestedPath(const std::string& parent, const std::string& child)
{
    return !parent.empty() && child.size() > parent.size() && child.find(parent + "/") == 0;
}

bool sameOrNestedPath(const std::string& parent, const std::string& child)
{
    return parent == child || isNestedPath(parent, child);
}

std::string stripTrailingSlashes(const std::string& value)
{
    std::string out = value;
    while (out.size() > 1 && (out[out.size() - 1] == '/' || out[out.size() - 1] == '\\')) {
        out.erase(out.size() - 1);
    }
    return out;
}

std::string currentWorkingDirectory()
{
    char buffer[4096];
#if defined(_WIN32)
    if (_getcwd(buffer, sizeof(buffer))) {
        return stripTrailingSlashes(buffer);
    }
#else
    if (getcwd(buffer, sizeof(buffer))) {
        return stripTrailingSlashes(buffer);
    }
#endif
    return ".";
}

std::string comparablePath(const std::string& path)
{
    if (path.empty()) {
        return "";
    }
    const std::string normalized = stripTrailingSlashes(path);
    if (isAbsolutePath(normalized)) {
        return normalized;
    }
    return stripTrailingSlashes(joinPath(currentWorkingDirectory(), normalized));
}

bool hasParentTraversal(const std::string& path)
{
    std::string token;
    for (std::size_t i = 0; i <= path.size(); ++i) {
        const char c = i < path.size() ? path[i] : '/';
        if (c == '/' || c == '\\') {
            if (token == "..") {
                return true;
            }
            token.clear();
        } else {
            token.push_back(c);
        }
    }
    return false;
}

std::string jsonStringArray(const std::vector<std::string>& values)
{
    std::ostringstream os;
    os << "[";
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i) {
            os << ",";
        }
        os << "\"" << escapeJson(values[i]) << "\"";
    }
    os << "]";
    return os.str();
}

std::string patientJson(const Patient& p)
{
    std::ostringstream os;
    os << "{"
       << jsonPair("id", p.id) << ","
       << jsonPair("patientNo", p.patientNo) << ","
       << jsonPair("orderNo", p.orderNo) << ","
       << jsonPair("name", p.name) << ","
       << jsonPair("gender", p.gender) << ","
       << jsonPair("age", p.age) << ","
       << jsonPair("phone", p.phone) << ","
       << jsonPair("doctor", p.doctor) << ","
       << jsonPair("remark", p.remark) << ","
       << jsonPair("createdAt", p.createdAt)
       << "}";
    return os.str();
}

std::string orderJson(const Order& order)
{
    std::ostringstream os;
    os << "{"
       << jsonPair("id", order.id) << ","
       << jsonPair("patientId", order.patientId) << ","
       << jsonPair("orderNo", order.orderNo) << ","
       << jsonPair("status", order.status) << ","
       << jsonPair("createdAt", order.createdAt) << ","
       << jsonPair("scanCount", order.scanCount) << ","
       << jsonPair("previewPath", order.previewPath) << ","
       << jsonPair("plyPath", order.plyPath)
       << "}";
    return os.str();
}

std::string pointCloudJson(int orderId, const PointCloudBuildResult& result, const std::string& previewPath)
{
    std::ostringstream os;
    os << "{"
       << jsonPair("orderId", orderId) << ","
       << jsonPair("plyPath", result.plyPath) << ","
       << jsonPair("previewPath", previewPath) << ","
       << jsonPair("pointCount", result.pointCount) << ","
       << "\"bounds\":{"
       << "\"minX\":" << toJsonNumber(result.minX) << ","
       << "\"minY\":" << toJsonNumber(result.minY) << ","
       << "\"minZ\":" << toJsonNumber(result.minZ) << ","
       << "\"maxX\":" << toJsonNumber(result.maxX) << ","
       << "\"maxY\":" << toJsonNumber(result.maxY) << ","
       << "\"maxZ\":" << toJsonNumber(result.maxZ)
       << "},\"sourceImages\":" << jsonStringArray(result.sourceImages) << "}";
    return os.str();
}

Patient patientFromBody(const std::string& body)
{
    Patient p;
    p.id = 0;
    p.patientNo = "";
    p.orderNo = "";
    p.name = jsonStringValue(body, "name");
    p.gender = jsonStringValue(body, "gender");
    p.age = jsonIntValue(body, "age");
    p.phone = jsonStringValue(body, "phone");
    p.doctor = jsonStringValue(body, "doctor");
    p.remark = jsonStringValue(body, "remark");
    p.createdAt = nowText();
    if (p.name.empty()) {
        p.name = "未命名患者";
    }
    return p;
}

struct PlyPreviewPoint {
    double x;
    double y;
    double z;
    double screenX;
    double screenY;
    double depth;
    int r;
    int g;
    int b;
};

std::string fileNameOnly(const std::string& path)
{
    const std::size_t pos = path.find_last_of("/\\");
    return pos == std::string::npos ? path : path.substr(pos + 1);
}

bool looksLikePatientNo(const std::string& value)
{
    if (value.size() < 2 || (value[0] != 'P' && value[0] != 'p')) {
        return false;
    }
    for (std::size_t i = 1; i < value.size(); ++i) {
        if (!std::isdigit(static_cast<unsigned char>(value[i]))) {
            return false;
        }
    }
    return true;
}

std::string extensionLower(const std::string& path)
{
    const std::size_t pos = path.find_last_of('.');
    std::string ext = pos == std::string::npos ? "" : path.substr(pos + 1);
    for (std::size_t i = 0; i < ext.size(); ++i) {
        ext[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(ext[i])));
    }
    return ext;
}

std::string removeExtension(const std::string& name)
{
    const std::size_t pos = name.find_last_of('.');
    return pos == std::string::npos ? name : name.substr(0, pos);
}

std::string pointCloudPreviewPathForPly(const std::string& plyPath)
{
    if (plyPath.empty()) {
        return "";
    }
    const std::string folder = parentDirectory(plyPath);
    const std::string baseName = removeExtension(fileNameOnly(plyPath));
    if (folder.empty() || baseName.empty()) {
        return "";
    }
    const std::string previewPath = folder + "/" + baseName + "_pointcloud_view.svg";
    return pathExists(previewPath) ? previewPath : "";
}

struct DirectoryEntry {
    std::string name;
    std::string path;
    bool directory;
    std::time_t modified;
};

bool hiddenName(const std::string& name)
{
    return name.empty() || name[0] == '.';
}

std::string formatTime(std::time_t value)
{
    if (value <= 0) {
        return nowText();
    }
    std::tm tmValue;
#if defined(_WIN32)
    localtime_s(&tmValue, &value);
#else
    localtime_r(&value, &tmValue);
#endif
    std::ostringstream os;
    os << std::put_time(&tmValue, "%Y-%m-%d %H:%M:%S");
    return os.str();
}

std::string createdAtFromCode(const std::string& value, std::time_t fallback)
{
    std::size_t start = 0;
    if (!value.empty() && (value[0] == 'P' || value[0] == 'p')) {
        start = 1;
    }
    if (value.size() >= start + 14) {
        bool digits = true;
        for (std::size_t i = start; i < start + 14; ++i) {
            if (!std::isdigit(static_cast<unsigned char>(value[i]))) {
                digits = false;
                break;
            }
        }
        if (digits) {
            return value.substr(start, 4) + "-" + value.substr(start + 4, 2) + "-" + value.substr(start + 6, 2)
                + " " + value.substr(start + 8, 2) + ":" + value.substr(start + 10, 2) + ":" + value.substr(start + 12, 2);
        }
    }
    return formatTime(fallback);
}

std::vector<DirectoryEntry> listDirectoryEntries(const std::string& path)
{
    std::vector<DirectoryEntry> entries;
#if defined(_WIN32)
    const std::string pattern = path + "\\*";
    WIN32_FIND_DATAA data;
    HANDLE handle = FindFirstFileA(pattern.c_str(), &data);
    if (handle == INVALID_HANDLE_VALUE) {
        return entries;
    }
    do {
        const std::string name = data.cFileName;
        if (name == "." || name == "..") {
            continue;
        }
        DirectoryEntry entry;
        entry.name = name;
        entry.path = path + "/" + name;
        entry.directory = (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        entry.modified = 0;
        entries.push_back(entry);
    } while (FindNextFileA(handle, &data));
    FindClose(handle);
#else
    DIR* dir = opendir(path.c_str());
    if (!dir) {
        return entries;
    }
    while (dirent* raw = readdir(dir)) {
        const std::string name = raw->d_name;
        if (name == "." || name == "..") {
            continue;
        }
        const std::string fullPath = path + "/" + name;
        struct stat info;
        if (lstat(fullPath.c_str(), &info) != 0) {
            continue;
        }
        DirectoryEntry entry;
        entry.name = name;
        entry.path = fullPath;
        entry.directory = S_ISDIR(info.st_mode);
        entry.modified = info.st_mtime;
        entries.push_back(entry);
    }
    closedir(dir);
#endif
    std::sort(entries.begin(), entries.end(), [](const DirectoryEntry& a, const DirectoryEntry& b) {
        return a.name < b.name;
    });
    return entries;
}

bool parsePatientFolder(const DirectoryEntry& entry, DataRootPatient* patient)
{
    if (!patient || !entry.directory || hiddenName(entry.name) || entry.name == "db") {
        return false;
    }
    const std::size_t dash = entry.name.find('-');
    if (dash == std::string::npos || dash == 0) {
        return false;
    }
    patient->patientNo = entry.name.substr(0, dash);
    if (!looksLikePatientNo(patient->patientNo)) {
        return false;
    }
    patient->name = dash + 1 < entry.name.size() ? entry.name.substr(dash + 1) : patient->patientNo;
    patient->createdAt = createdAtFromCode(patient->patientNo, entry.modified);
    return true;
}

bool imageExtension(const std::string& ext)
{
    return ext == "svg" || ext == "png" || ext == "jpg" || ext == "jpeg" || ext == "gif" || ext == "ppm";
}

DataRootOrder scanOrderDirectory(const DirectoryEntry& entry)
{
    DataRootOrder order;
    order.orderNo = entry.name;
    order.createdAt = createdAtFromCode(order.orderNo, entry.modified);
    const std::vector<DirectoryEntry> files = listDirectoryEntries(entry.path);
    for (std::size_t i = 0; i < files.size(); ++i) {
        if (files[i].directory || hiddenName(files[i].name)) {
            continue;
        }
        const std::string ext = extensionLower(files[i].name);
        if (ext == "ply" && order.plyPath.empty()) {
            order.plyPath = files[i].path;
            continue;
        }
        if (!imageExtension(ext)) {
            continue;
        }
        if (files[i].name.find("pointcloud_view") != std::string::npos) {
            order.previewPath = files[i].path;
            continue;
        }
        order.imagePaths.push_back(files[i].path);
        if (order.previewPath.empty() && files[i].name.find("_front.") != std::string::npos) {
            order.previewPath = files[i].path;
        }
    }
    if (order.previewPath.empty() && !order.imagePaths.empty()) {
        order.previewPath = order.imagePaths[0];
    }
    return order;
}

std::vector<DataRootPatient> scanDataRoot(const std::string& imageRoot)
{
    std::vector<DataRootPatient> patients;
    const std::vector<DirectoryEntry> entries = listDirectoryEntries(imageRoot);
    for (std::size_t i = 0; i < entries.size(); ++i) {
        DataRootPatient patient;
        if (!parsePatientFolder(entries[i], &patient)) {
            continue;
        }
        const std::vector<DirectoryEntry> children = listDirectoryEntries(entries[i].path);
        for (std::size_t j = 0; j < children.size(); ++j) {
            if (!children[j].directory || hiddenName(children[j].name)) {
                continue;
            }
            patient.orders.push_back(scanOrderDirectory(children[j]));
        }
        patients.push_back(patient);
    }
    return patients;
}

bool movePatientDirectories(const std::string& fromRoot, const std::string& toRoot)
{
    if (fromRoot.empty() || toRoot.empty() || fromRoot == toRoot || !pathExists(fromRoot)) {
        return true;
    }
    ensureDirectory(toRoot);
    const std::vector<DirectoryEntry> entries = listDirectoryEntries(fromRoot);
    for (std::size_t i = 0; i < entries.size(); ++i) {
        DataRootPatient patient;
        if (!parsePatientFolder(entries[i], &patient)) {
            continue;
        }
        const std::string target = toRoot + "/" + entries[i].name;
        if (!movePathRecursive(entries[i].path, target)) {
            return false;
        }
    }
    return true;
}

std::vector<unsigned char> readFileBytes(const std::string& path)
{
    std::ifstream file(path.c_str(), std::ios::binary);
    if (!file) {
        return std::vector<unsigned char>();
    }
    file.seekg(0, std::ios::end);
    const std::streamoff size = file.tellg();
    if (size < 0) {
        return std::vector<unsigned char>();
    }
    file.seekg(0, std::ios::beg);
    std::vector<unsigned char> bytes(static_cast<std::size_t>(size));
    if (!bytes.empty()) {
        file.read(reinterpret_cast<char*>(&bytes[0]), size);
    }
    return bytes;
}

void writeUInt16(std::ofstream& out, std::uint16_t value)
{
    out.put(static_cast<char>(value & 0xff));
    out.put(static_cast<char>((value >> 8) & 0xff));
}

void writeUInt32(std::ofstream& out, std::uint32_t value)
{
    out.put(static_cast<char>(value & 0xff));
    out.put(static_cast<char>((value >> 8) & 0xff));
    out.put(static_cast<char>((value >> 16) & 0xff));
    out.put(static_cast<char>((value >> 24) & 0xff));
}

std::uint32_t crc32Bytes(const std::vector<unsigned char>& bytes)
{
    std::uint32_t crc = 0xffffffffu;
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        crc ^= bytes[i];
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
        }
    }
    return crc ^ 0xffffffffu;
}

bool deflateRaw(const std::vector<unsigned char>& input, std::vector<unsigned char>* output)
{
    if (!output || input.empty()) {
        return false;
    }
    z_stream stream;
    std::memset(&stream, 0, sizeof(stream));
    if (deflateInit2(&stream, Z_DEFAULT_COMPRESSION, Z_DEFLATED, -MAX_WBITS, 8, Z_DEFAULT_STRATEGY) != Z_OK) {
        return false;
    }

    output->clear();
    output->resize(input.size() + input.size() / 16 + 64);
    stream.next_in = const_cast<Bytef*>(reinterpret_cast<const Bytef*>(&input[0]));
    stream.avail_in = static_cast<uInt>(input.size());
    stream.next_out = reinterpret_cast<Bytef*>(&(*output)[0]);
    stream.avail_out = static_cast<uInt>(output->size());

    int status = deflate(&stream, Z_FINISH);
    while (status == Z_OK) {
        const std::size_t used = output->size() - stream.avail_out;
        output->resize(output->size() * 2);
        stream.next_out = reinterpret_cast<Bytef*>(&(*output)[used]);
        stream.avail_out = static_cast<uInt>(output->size() - used);
        status = deflate(&stream, Z_FINISH);
    }
    const bool ok = status == Z_STREAM_END;
    if (ok) {
        output->resize(output->size() - stream.avail_out);
    }
    deflateEnd(&stream);
    return ok;
}

bool writeZipDeflate(const std::string& zipPath, const std::vector<std::string>& filePaths)
{
    struct Entry {
        std::string name;
        std::vector<unsigned char> bytes;
        std::vector<unsigned char> zipBytes;
        std::uint32_t crc;
        std::uint32_t offset;
        std::uint16_t method;
    };

    std::vector<Entry> entries;
    for (std::size_t i = 0; i < filePaths.size(); ++i) {
        const std::vector<unsigned char> bytes = readFileBytes(filePaths[i]);
        if (bytes.empty()) {
            continue;
        }
        Entry entry;
        entry.name = fileNameOnly(filePaths[i]);
        entry.bytes = bytes;
        entry.method = 8;
        if (!deflateRaw(entry.bytes, &entry.zipBytes)) {
            entry.zipBytes = entry.bytes;
            entry.method = 0;
        }
        entry.crc = crc32Bytes(entry.bytes);
        entry.offset = 0;
        entries.push_back(entry);
    }
    if (entries.empty()) {
        return false;
    }

    ensureDirectory(parentDirectory(zipPath));
    std::ofstream out(zipPath.c_str(), std::ios::binary | std::ios::trunc);
    if (!out) {
        return false;
    }

    for (std::size_t i = 0; i < entries.size(); ++i) {
        Entry& entry = entries[i];
        entry.offset = static_cast<std::uint32_t>(out.tellp());
        writeUInt32(out, 0x04034b50u);
        writeUInt16(out, 20);
        writeUInt16(out, 0);
        writeUInt16(out, entry.method);
        writeUInt16(out, 0);
        writeUInt16(out, 0);
        writeUInt32(out, entry.crc);
        writeUInt32(out, static_cast<std::uint32_t>(entry.zipBytes.size()));
        writeUInt32(out, static_cast<std::uint32_t>(entry.bytes.size()));
        writeUInt16(out, static_cast<std::uint16_t>(entry.name.size()));
        writeUInt16(out, 0);
        out.write(entry.name.c_str(), static_cast<std::streamsize>(entry.name.size()));
        out.write(reinterpret_cast<const char*>(&entry.zipBytes[0]), static_cast<std::streamsize>(entry.zipBytes.size()));
    }

    const std::uint32_t centralOffset = static_cast<std::uint32_t>(out.tellp());
    for (std::size_t i = 0; i < entries.size(); ++i) {
        const Entry& entry = entries[i];
        writeUInt32(out, 0x02014b50u);
        writeUInt16(out, 20);
        writeUInt16(out, 20);
        writeUInt16(out, 0);
        writeUInt16(out, entry.method);
        writeUInt16(out, 0);
        writeUInt16(out, 0);
        writeUInt32(out, entry.crc);
        writeUInt32(out, static_cast<std::uint32_t>(entry.zipBytes.size()));
        writeUInt32(out, static_cast<std::uint32_t>(entry.bytes.size()));
        writeUInt16(out, static_cast<std::uint16_t>(entry.name.size()));
        writeUInt16(out, 0);
        writeUInt16(out, 0);
        writeUInt16(out, 0);
        writeUInt16(out, 0);
        writeUInt32(out, 0);
        writeUInt32(out, entry.offset);
        out.write(entry.name.c_str(), static_cast<std::streamsize>(entry.name.size()));
    }
    const std::uint32_t centralSize = static_cast<std::uint32_t>(out.tellp()) - centralOffset;
    writeUInt32(out, 0x06054b50u);
    writeUInt16(out, 0);
    writeUInt16(out, 0);
    writeUInt16(out, static_cast<std::uint16_t>(entries.size()));
    writeUInt16(out, static_cast<std::uint16_t>(entries.size()));
    writeUInt32(out, centralSize);
    writeUInt32(out, centralOffset);
    writeUInt16(out, 0);
    return true;
}

std::string contentTypeForImagePath(const std::string& path)
{
    const std::string ext = extensionLower(path);
    if (ext == "svg") return "image/svg+xml; charset=utf-8";
    if (ext == "png") return "image/png";
    if (ext == "jpg" || ext == "jpeg") return "image/jpeg";
    if (ext == "gif") return "image/gif";
    if (ext == "ppm") return "image/x-portable-pixmap";
    return "application/octet-stream";
}

std::string buildPointCloudPreviewSvg(const std::string& plyPath, const std::string& previewPath)
{
    std::ifstream in(plyPath.c_str(), std::ios::binary);
    if (!in) {
        return "";
    }
    std::string line;
    bool inHeader = true;
    int vertexCount = 0;
    std::vector<PlyPreviewPoint> points;
    points.reserve(22000);
    double minX = std::numeric_limits<double>::max();
    double maxX = -std::numeric_limits<double>::max();
    double minY = std::numeric_limits<double>::max();
    double maxY = -std::numeric_limits<double>::max();
    double minZ = std::numeric_limits<double>::max();
    double maxZ = -std::numeric_limits<double>::max();
    int vertexIndex = 0;
    int sampleStep = 1;
    while (std::getline(in, line)) {
        if (inHeader) {
            if (line.find("element vertex ") == 0) {
                vertexCount = std::atoi(line.substr(std::string("element vertex ").size()).c_str());
                sampleStep = std::max(1, vertexCount / 22000);
            }
            if (line == "end_header") {
                inHeader = false;
            }
            continue;
        }
        std::istringstream row(line);
        PlyPreviewPoint p;
        if (!(row >> p.x >> p.y >> p.z >> p.r >> p.g >> p.b)) {
            continue;
        }
        minX = std::min(minX, p.x);
        maxX = std::max(maxX, p.x);
        minY = std::min(minY, p.y);
        maxY = std::max(maxY, p.y);
        minZ = std::min(minZ, p.z);
        maxZ = std::max(maxZ, p.z);
        if (vertexIndex % sampleStep == 0) {
            points.push_back(p);
        }
        ++vertexIndex;
    }
    if (points.empty()) {
        return "";
    }

    const double centerX = (minX + maxX) * 0.5;
    const double centerY = (minY + maxY) * 0.5;
    const double centerZ = (minZ + maxZ) * 0.5;
    const double yaw = -0.28;
    const double pitch = 0.10;
    const double cy = std::cos(yaw);
    const double sy = std::sin(yaw);
    const double cp = std::cos(pitch);
    const double sp = std::sin(pitch);
    double minSX = std::numeric_limits<double>::max();
    double maxSX = -std::numeric_limits<double>::max();
    double minSY = std::numeric_limits<double>::max();
    double maxSY = -std::numeric_limits<double>::max();
    const double boundsX[] = { minX, maxX };
    const double boundsY[] = { minY, maxY };
    const double boundsZ[] = { minZ, maxZ };
    for (int ix = 0; ix < 2; ++ix) {
        for (int iy = 0; iy < 2; ++iy) {
            for (int iz = 0; iz < 2; ++iz) {
                const double dx = boundsX[ix] - centerX;
                const double dy = boundsY[iy] - centerY;
                const double dz = boundsZ[iz] - centerZ;
                const double rx = dx * cy - dz * sy;
                const double rz = dx * sy + dz * cy;
                const double ry = dy * cp - rz * sp;
                minSX = std::min(minSX, rx);
                maxSX = std::max(maxSX, rx);
                minSY = std::min(minSY, ry);
                maxSY = std::max(maxSY, ry);
            }
        }
    }
    for (std::size_t i = 0; i < points.size(); ++i) {
        const double dx = points[i].x - centerX;
        const double dy = points[i].y - centerY;
        const double dz = points[i].z - centerZ;
        const double rx = dx * cy - dz * sy;
        const double rz = dx * sy + dz * cy;
        const double ry = dy * cp - rz * sp;
        const double depth = dy * sp + rz * cp;
        points[i].screenX = rx;
        points[i].screenY = ry;
        points[i].depth = depth;
        minSX = std::min(minSX, rx);
        maxSX = std::max(maxSX, rx);
        minSY = std::min(minSY, ry);
        maxSY = std::max(maxSY, ry);
    }
    if (maxSX - minSX < 0.001) maxSX = minSX + 1.0;
    if (maxSY - minSY < 0.001) maxSY = minSY + 1.0;
    std::sort(points.begin(), points.end(), [](const PlyPreviewPoint& a, const PlyPreviewPoint& b) {
        return a.depth < b.depth;
    });

    ensureDirectory(parentDirectory(previewPath));
    std::ofstream out(previewPath.c_str(), std::ios::binary | std::ios::trunc);
    if (!out) {
        return "";
    }
    const double width = 720.0;
    const double height = 480.0;
    const double pad = 26.0;
    const double scale = std::min((width - pad * 2.0) / (maxSX - minSX), (height - pad * 2.0) / (maxSY - minSY));
    const double offsetX = (width - (maxSX - minSX) * scale) * 0.5;
    const double offsetY = (height - (maxSY - minSY) * scale) * 0.5;
    const double radius = vertexCount > 60000 ? 1.25 : 1.55;
    out << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"720\" height=\"480\" viewBox=\"0 0 720 480\">";
    out << "<rect width=\"720\" height=\"480\" fill=\"#f5f8fa\"/>";
    out << "<g opacity=\"0.96\">";
    for (std::size_t i = 0; i < points.size(); ++i) {
        const double x = offsetX + (points[i].screenX - minSX) * scale;
        const double y = offsetY + (maxSY - points[i].screenY) * scale;
        out << "<circle cx=\"" << x << "\" cy=\"" << y << "\" r=\"" << radius << "\" fill=\"rgb("
            << points[i].r << "," << points[i].g << "," << points[i].b << ")\"/>";
    }
    out << "</g>";
    out << "</svg>";
    return previewPath;
}

} // namespace

class App::Impl {
public:
    explicit Impl(const AppConfig& config)
        : database_(config.databasePath),
          camera_(config.imageRoot, config.cameraMode),
          imageRoot_(CameraManager::normalizeRoot(config.imageRoot)),
          config_(config),
          backendRoot_(backendRootFromConfigPath(config.configPath))
    {
        config_.imageRoot = imageRoot_;
    }

    http::response<http::string_body> handle(const http::request<http::string_body>& req)
    {
        const std::string target(req.target().data(), req.target().size());
        const std::string path = pathOnly(target);
        try {
            if (req.method() == http::verb::options) {
                return textResponse(req, http::status::no_content, "");
            }
            if (req.method() == http::verb::post && path == "/api/login") {
                return jsonResponse(req, "{\"token\":\"dev-token\",\"user\":{\"name\":\"管理员\"}}");
            }
            if (req.method() == http::verb::get && path == "/api/health") {
                return jsonResponse(req, "{\"status\":\"ok\"}");
            }
            if (req.method() == http::verb::get && path == "/api/settings") {
                return settings(req);
            }
            if (req.method() == http::verb::put && path == "/api/settings") {
                return updateSettings(req);
            }
            if (req.method() == http::verb::post && path == "/api/settings/open-data-root") {
                return openDataRoot(req);
            }
            if (req.method() == http::verb::post && path == "/api/settings/select-data-root") {
                return selectDataRoot(req);
            }
            if (req.method() == http::verb::post && path == "/api/settings/validate-data-root") {
                return validateDataRoot(req);
            }
            if (req.method() == http::verb::get && path == "/api/patients") {
                return listPatients(req, target);
            }
            if (req.method() == http::verb::post && path == "/api/patients") {
                Patient p = patientFromBody(req.body());
                const int id = database_.createPatient(p);
                Patient created = database_.patientById(id);
                ensureDirectory(patientDirectory(imageRoot_, created));
                const int orderId = database_.latestOrderId(id);
                const std::string createdOrderNo = database_.orderNo(orderId);
                if (!createdOrderNo.empty()) {
                    ensureDirectory(orderDirectory(imageRoot_, created, createdOrderNo));
                }
                return jsonResponse(req, "{\"id\":" + toString(id)
                    + ",\"orderId\":" + toString(orderId)
                    + ",\"patientNo\":\"" + escapeJson(created.patientNo)
                    + "\",\"orderNo\":\"" + escapeJson(createdOrderNo) + "\"}");
            }
            if (req.method() == http::verb::put && path.find("/api/patients/") == 0) {
                const int id = std::atoi(path.substr(std::string("/api/patients/").size()).c_str());
                const Patient before = database_.patientById(id);
                const std::string oldFolder = before.id == 0 ? "" : existingPatientDirectory(imageRoot_, before);
                const bool ok = database_.updatePatient(id, patientFromBody(req.body()));
                if (ok && before.id != 0) {
                    const Patient after = database_.patientById(id);
                    const std::string newFolder = patientDirectory(imageRoot_, after);
                    if (oldFolder != newFolder) {
                        if (movePathRecursive(oldFolder, newFolder)) {
                            database_.replaceStoredPathPrefix(oldFolder, newFolder);
                        } else {
                            return jsonResponse(req, "{\"ok\":false,\"error\":\"patient folder rename failed\"}", http::status::internal_server_error);
                        }
                    }
                }
                return jsonResponse(req, std::string("{\"ok\":") + (ok ? "true" : "false") + "}");
            }
            if (req.method() == http::verb::delete_ && path.find("/api/patients/") == 0) {
                const int id = std::atoi(path.substr(std::string("/api/patients/").size()).c_str());
                const Patient patient = database_.patientById(id);
                const std::string folder = patient.id == 0 ? "" : patientDirectory(imageRoot_, patient);
                const std::string legacyFolder = patient.id == 0 ? "" : legacyPatientDirectory(imageRoot_, patient);
                const bool ok = database_.deletePatient(id);
                if (ok && !folder.empty()) {
                    removePathRecursive(folder);
                }
                if (ok && !legacyFolder.empty() && legacyFolder != folder) {
                    removePathRecursive(legacyFolder);
                }
                return jsonResponse(req, std::string("{\"ok\":") + (ok ? "true" : "false") + "}");
            }
            if (req.method() == http::verb::get && path.find("/api/patients/") == 0 && path.find("/orders") != std::string::npos) {
                const std::string prefix = "/api/patients/";
                const std::size_t end = path.find("/orders");
                const int patientId = std::atoi(path.substr(prefix.size(), end - prefix.size()).c_str());
                return listOrders(req, patientId);
            }
            if (req.method() == http::verb::get && path.find("/api/patients/") == 0 && path.find("/scans") != std::string::npos) {
                const std::string prefix = "/api/patients/";
                const std::size_t end = path.find("/scans");
                const int patientId = std::atoi(path.substr(prefix.size(), end - prefix.size()).c_str());
                return listScans(req, patientId);
            }
            if (req.method() == http::verb::post && path == "/api/orders") {
                const int patientId = jsonIntValue(req.body(), "patientId");
                const int orderId = database_.createOrder(patientId);
                const Patient patient = database_.patientById(patientId);
                const std::string orderNo = database_.orderNo(orderId);
                if (patient.id != 0 && !orderNo.empty()) {
                    ensureDirectory(orderDirectory(imageRoot_, patient, orderNo));
                }
                return jsonResponse(req, "{\"id\":" + toString(orderId) + ",\"orderNo\":\"" + escapeJson(orderNo) + "\"}");
            }
            if (req.method() == http::verb::delete_ && path.find("/api/orders/") == 0) {
                const int id = std::atoi(path.substr(std::string("/api/orders/").size()).c_str());
                const Patient patient = database_.patientByOrderId(id);
                const std::string orderNo = database_.orderNo(id);
                const std::string folder = patient.id == 0 || orderNo.empty() ? "" : orderDirectory(imageRoot_, patient, orderNo);
                const bool ok = database_.deleteOrder(id);
                if (ok && !folder.empty()) {
                    removePathRecursive(folder);
                }
                return jsonResponse(req, std::string("{\"ok\":") + (ok ? "true" : "false") + "}");
            }
            if (req.method() == http::verb::post && path.find("/api/orders/") == 0 && path.find("/reconstruct") != std::string::npos) {
                const std::string prefix = "/api/orders/";
                const std::size_t end = path.find("/reconstruct");
                const int orderId = std::atoi(path.substr(prefix.size(), end - prefix.size()).c_str());
                return reconstructOrder(req, orderId);
            }
            if (req.method() == http::verb::post && path.find("/api/orders/") == 0 && path.find("/open-folder") != std::string::npos) {
                const std::string prefix = "/api/orders/";
                const std::size_t end = path.find("/open-folder");
                const int orderId = std::atoi(path.substr(prefix.size(), end - prefix.size()).c_str());
                return openOrderFolder(req, orderId);
            }
            if (req.method() == http::verb::post && path == "/api/camera/start") {
                camera_.start();
                return jsonResponse(req, "{\"streaming\":true}");
            }
            if (req.method() == http::verb::post && path == "/api/camera/stop") {
                camera_.stop();
                return jsonResponse(req, "{\"streaming\":false}");
            }
            if (req.method() == http::verb::get && path == "/api/camera/frame") {
                std::map<std::string, std::string> query = parseQuery(target);
                const std::string view = query.count("view") ? query["view"] : "front";
                const CameraImage image = camera_.frameImage(view);
                return textResponse(req, http::status::ok, image.body, image.contentType);
            }
            if (req.method() == http::verb::get && path == "/api/pointcloud/file") {
                return pointCloudFile(req, target);
            }
            if (req.method() == http::verb::get && path == "/api/files/image") {
                return imageFile(req, target);
            }
            if (req.method() == http::verb::post && path == "/api/camera/capture") {
                return capture(req);
            }
            if (req.method() == http::verb::post && path == "/api/export/package") {
                return packageOrder(req);
            }
            if (req.method() == http::verb::post && path == "/api/export/upload") {
                return jsonResponse(req, "{\"ok\":true,\"message\":\"云端上传接口已预留，第一阶段不启用真实云同步\"}");
            }
            return jsonResponse(req, "{\"error\":\"not found\"}", http::status::not_found);
        } catch (const std::exception& e) {
            return jsonResponse(req, "{\"error\":\"" + escapeJson(e.what()) + "\"}", http::status::internal_server_error);
        }
    }

private:
    Database database_;
    CameraManager camera_;
    std::string imageRoot_;
    AppConfig config_;
    std::string backendRoot_;

    http::response<http::string_body> textResponse(
        const http::request<http::string_body>& req,
        http::status status,
        const std::string& body,
        const std::string& contentType = "text/plain; charset=utf-8")
    {
        http::response<http::string_body> res(status, req.version());
        res.set(http::field::server, "FaceScan Backend");
        res.set(http::field::content_type, contentType);
        res.set(http::field::access_control_allow_origin, "*");
        res.set(http::field::access_control_allow_headers, "Content-Type, Authorization");
        res.set(http::field::access_control_allow_methods, "GET, POST, PUT, DELETE, OPTIONS");
        res.keep_alive(req.keep_alive());
        res.body() = body;
        res.prepare_payload();
        return res;
    }

    http::response<http::string_body> jsonResponse(
        const http::request<http::string_body>& req,
        const std::string& body,
        http::status status = http::status::ok)
    {
        return textResponse(req, status, body, "application/json; charset=utf-8");
    }

    http::response<http::string_body> svgResponse(const http::request<http::string_body>& req, const std::string& body)
    {
        return textResponse(req, http::status::ok, body, "image/svg+xml; charset=utf-8");
    }

    http::response<http::string_body> plyResponse(const http::request<http::string_body>& req, const std::string& body)
    {
        return textResponse(req, http::status::ok, body, "application/ply; charset=utf-8");
    }

    http::response<http::string_body> binaryFileResponse(
        const http::request<http::string_body>& req,
        const std::vector<unsigned char>& bytes,
        const std::string& contentType)
    {
        std::string body;
        if (!bytes.empty()) {
            body.assign(reinterpret_cast<const char*>(&bytes[0]), bytes.size());
        }
        return textResponse(req, http::status::ok, body, contentType);
    }

    http::response<http::string_body> settings(const http::request<http::string_body>& req)
    {
        return jsonResponse(req, "{\"dataRoot\":\"" + escapeJson(imageRoot_)
            + "\",\"databasePath\":\"" + escapeJson(config_.databasePath)
            + "\",\"configPath\":\"" + escapeJson(config_.configPath)
            + "\",\"cameraMode\":\"" + escapeJson(config_.cameraMode) + "\"}");
    }

    std::string resolveDataRootInput(const std::string& value) const
    {
        std::string trimmed = value;
        while (!trimmed.empty() && std::isspace(static_cast<unsigned char>(trimmed[0]))) {
            trimmed.erase(0, 1);
        }
        while (!trimmed.empty() && std::isspace(static_cast<unsigned char>(trimmed[trimmed.size() - 1]))) {
            trimmed.erase(trimmed.size() - 1);
        }
        if (trimmed.empty()) {
            return "";
        }
        const std::string normalized = CameraManager::normalizeRoot(trimmed);
        if (normalized.empty()) {
            return "";
        }
        if (isAbsolutePath(normalized) || (!backendRoot_.empty() && backendRoot_ != "." && normalized.find(backendRoot_ + "/") == 0)) {
            return normalized;
        }
        return joinPath(backendRoot_, normalized);
    }

    std::string dataRootValidationError(const std::string& dataRoot) const
    {
        if (dataRoot.empty()) {
            return "请输入数据保存目录";
        }
        if (hasParentTraversal(dataRoot)) {
            return "目录不能包含上级路径符号";
        }
        if (dataRoot == "/" || dataRoot == "." || dataRoot == "..") {
            return "目录路径不合法";
        }
        const std::string databaseDir = parentDirectory(config_.databasePath);
        const std::string systemDataDir = parentDirectory(databaseDir);
        const std::string comparableDataRoot = comparablePath(dataRoot);
        const std::string comparableDatabaseDir = comparablePath(databaseDir);
        const std::string comparableSystemDataDir = comparablePath(systemDataDir);
        const std::string comparableBackendRoot = comparablePath(backendRoot_);
        const std::string comparableCurrentRoot = comparablePath(imageRoot_);
        if (!databaseDir.empty() && sameOrNestedPath(comparableDatabaseDir, comparableDataRoot)) {
            return "数据保存目录不能设置为数据库目录";
        }
        if (!systemDataDir.empty() && comparableDataRoot == comparableSystemDataDir) {
            return "数据保存目录不能设置为后端固定数据目录，请选择其下的 images 目录或其它患者数据目录";
        }
        if (!backendRoot_.empty() && comparableDataRoot == comparableBackendRoot) {
            return "数据保存目录不能设置为后端工程目录";
        }
        if ((isNestedPath(comparableCurrentRoot, comparableDataRoot) || isNestedPath(comparableDataRoot, comparableCurrentRoot))
            && comparableCurrentRoot != comparableDataRoot) {
            return "新目录不能是当前数据目录的父目录或子目录";
        }
        return "";
    }

    http::response<http::string_body> validateDataRoot(const http::request<http::string_body>& req)
    {
        const std::string dataRoot = resolveDataRootInput(jsonStringValue(req.body(), "dataRoot"));
        const std::string error = dataRootValidationError(dataRoot);
        if (!error.empty()) {
            return jsonResponse(req, "{\"ok\":false,\"message\":\"" + escapeJson(error)
                + "\",\"normalizedPath\":\"" + escapeJson(dataRoot) + "\"}", http::status::bad_request);
        }
        return jsonResponse(req, std::string("{\"ok\":true,\"needsMigration\":")
            + (dataRoot != imageRoot_ ? "true" : "false")
            + ",\"message\":\"目录路径可用\",\"normalizedPath\":\"" + escapeJson(dataRoot) + "\"}");
    }

    http::response<http::string_body> updateSettings(const http::request<http::string_body>& req)
    {
        const std::string dataRoot = resolveDataRootInput(jsonStringValue(req.body(), "dataRoot"));
        const std::string error = dataRootValidationError(dataRoot);
        if (!error.empty()) {
            return jsonResponse(req, "{\"error\":\"" + escapeJson(error) + "\"}", http::status::bad_request);
        }
        const std::string previousRoot = imageRoot_;
        if (previousRoot != dataRoot) {
            if (!movePatientDirectories(previousRoot, dataRoot)) {
                return jsonResponse(req, "{\"error\":\"data migration failed\"}", http::status::internal_server_error);
            }
            ensureDirectory(dataRoot);
            database_.replaceStoredPathPrefix(previousRoot, dataRoot);
        } else {
            ensureDirectory(dataRoot);
        }
        imageRoot_ = dataRoot;
        camera_.setImageRoot(dataRoot);
        config_.imageRoot = dataRoot;
        if (!saveAppConfig(config_)) {
            return jsonResponse(req, "{\"error\":\"settings save failed\"}", http::status::internal_server_error);
        }
        return settings(req);
    }

    http::response<http::string_body> openDataRoot(const http::request<http::string_body>& req)
    {
        ensureDirectory(imageRoot_);
        const bool ok = openDirectory(imageRoot_);
        return jsonResponse(req, std::string("{\"ok\":") + (ok ? "true" : "false")
            + ",\"path\":\"" + escapeJson(imageRoot_) + "\"}");
    }

    http::response<http::string_body> selectDataRoot(const http::request<http::string_body>& req)
    {
        const std::string path = resolveDataRootInput(chooseDirectory());
        if (path.empty()) {
            return jsonResponse(req, "{\"ok\":false,\"path\":\"\"}");
        }
        return jsonResponse(req, "{\"ok\":true,\"path\":\"" + escapeJson(path) + "\"}");
    }

    void syncDataRootIndex()
    {
        ensureDirectory(imageRoot_);
        database_.syncDataRoot(scanDataRoot(imageRoot_));
    }

    http::response<http::string_body> listPatients(const http::request<http::string_body>& req, const std::string& target)
    {
        syncDataRootIndex();
        std::map<std::string, std::string> query = parseQuery(target);
        const std::string keyword = query.count("keyword") ? query["keyword"] : "";
        const std::string date = query.count("date") ? query["date"] : "";
        const std::vector<Patient> list = database_.patients(keyword, date);
        std::ostringstream os;
        os << "{\"items\":[";
        for (std::size_t i = 0; i < list.size(); ++i) {
            if (i) os << ",";
            os << patientJson(list[i]);
        }
        os << "]}";
        return jsonResponse(req, os.str());
    }

    http::response<http::string_body> listScans(const http::request<http::string_body>& req, int patientId)
    {
        const std::vector<ScanResult> scans = database_.scansForPatient(patientId);
        std::ostringstream os;
        os << "{\"items\":[";
        for (std::size_t i = 0; i < scans.size(); ++i) {
            if (i) os << ",";
            os << "{"
               << jsonPair("id", scans[i].id) << ","
               << jsonPair("orderId", scans[i].orderId) << ","
               << jsonPair("plyPath", scans[i].plyPath) << ","
               << jsonPair("previewPath", scans[i].previewPath) << ","
               << "\"imagePaths\":" << jsonStringArray(scans[i].imagePaths) << ","
               << jsonPair("createdAt", scans[i].createdAt)
               << "}";
        }
        os << "]}";
        return jsonResponse(req, os.str());
    }

    http::response<http::string_body> listOrders(const http::request<http::string_body>& req, int patientId)
    {
        syncDataRootIndex();
        std::vector<Order> orders = database_.ordersForPatient(patientId);
        std::ostringstream os;
        os << "{\"items\":[";
        for (std::size_t i = 0; i < orders.size(); ++i) {
            if (i) os << ",";
            const std::string pointCloudPreview = pointCloudPreviewPathForPly(orders[i].plyPath);
            if (!pointCloudPreview.empty()) {
                orders[i].previewPath = pointCloudPreview;
            }
            os << orderJson(orders[i]);
        }
        os << "]}";
        return jsonResponse(req, os.str());
    }

    http::response<http::string_body> openOrderFolder(const http::request<http::string_body>& req, int orderId)
    {
        std::string folder = parentDirectory(database_.latestPreviewPathForOrder(orderId));
        if (folder.empty()) {
            folder = imageRoot_;
        }
        ensureDirectory(folder);
        const bool ok = openDirectory(folder);
        return jsonResponse(req, std::string("{\"ok\":") + (ok ? "true" : "false")
            + ",\"path\":\"" + escapeJson(folder) + "\"}");
    }

    http::response<http::string_body> capture(const http::request<http::string_body>& req)
    {
        const int patientId = jsonIntValue(req.body(), "patientId");
        const int orderId = jsonIntValue(req.body(), "orderId") > 0 ? jsonIntValue(req.body(), "orderId") : database_.latestOrderId(patientId);
        const Patient patient = database_.patientById(patientId);
        const std::string orderNo = database_.orderNo(orderId);
        if (patient.id == 0 || orderNo.empty()) {
            return jsonResponse(req, "{\"error\":\"invalid patient or order\"}", http::status::bad_request);
        }
        const std::string safeOrderName = orderFolderName(orderNo);
        const std::string folder = orderDirectory(imageRoot_, patient, orderNo);
        const std::vector<std::string> paths = camera_.capture(folder, safeOrderName);
        database_.replaceOrderCapture(orderId, paths);
        std::ostringstream os;
        os << "{\"orderId\":" << orderId << ",\"paths\":[";
        for (std::size_t i = 0; i < paths.size(); ++i) {
            if (i) os << ",";
            os << "\"" << escapeJson(paths[i]) << "\"";
        }
        os << "]}";
        return jsonResponse(req, os.str());
    }

    http::response<http::string_body> reconstructOrder(const http::request<http::string_body>& req, int orderId)
    {
        std::vector<std::string> imagePaths = database_.latestImagePathsForOrder(orderId, 4);
        if (imagePaths.empty()) {
            return jsonResponse(req, "{\"error\":\"no captured images for this order\"}", http::status::bad_request);
        }
        const Patient patient = database_.patientByOrderId(orderId);
        const std::string orderNo = database_.orderNo(orderId);
        if (patient.id == 0 || orderNo.empty()) {
            return jsonResponse(req, "{\"error\":\"invalid order\"}", http::status::bad_request);
        }
        const std::string safeOrderName = orderFolderName(orderNo);
        const std::string folder = orderDirectory(imageRoot_, patient, orderNo);
        PointCloudOptions options;
        options.namePrefix = safeOrderName;
        options.outputFileName = safeOrderName + ".ply";
        const int columns = jsonIntValue(req.body(), "columns");
        const int rows = jsonIntValue(req.body(), "rows");
        if (columns > 0) options.columns = columns;
        if (rows > 0) options.rows = rows;

        ColorPointCloudSdk sdk(folder);
        PointCloudBuildResult result = sdk.reconstruct(imagePaths, options);
        if (!result.ok) {
            return jsonResponse(req, "{\"error\":\"" + escapeJson(result.message) + "\"}", http::status::internal_server_error);
        }
        const std::string previewPath = buildPointCloudPreviewSvg(
            result.plyPath,
            folder + "/" + safeOrderName + "_pointcloud_view.svg");
        database_.setPointCloudResult(orderId, result.plyPath, previewPath);
        return jsonResponse(req, pointCloudJson(orderId, result, previewPath));
    }

    http::response<http::string_body> pointCloudFile(const http::request<http::string_body>& req, const std::string& target)
    {
        std::map<std::string, std::string> query = parseQuery(target);
        const std::string path = query.count("path") ? query["path"] : "";
        const bool underRoot = path.find(imageRoot_ + "/") == 0;
        const bool isPly = path.size() > 4 && path.substr(path.size() - 4) == ".ply";
        if (path.empty() || !underRoot || !isPly || path.find("..") != std::string::npos) {
            return jsonResponse(req, "{\"error\":\"invalid point cloud path\"}", http::status::bad_request);
        }
        const std::string body = readFileText(path);
        if (body.empty()) {
            return jsonResponse(req, "{\"error\":\"point cloud file not found\"}", http::status::not_found);
        }
        return plyResponse(req, body);
    }

    http::response<http::string_body> imageFile(const http::request<http::string_body>& req, const std::string& target)
    {
        std::map<std::string, std::string> query = parseQuery(target);
        const std::string path = query.count("path") ? query["path"] : "";
        const bool underRoot = path.find(imageRoot_ + "/") == 0;
        const std::string ext = extensionLower(path);
        const bool isImage = ext == "svg" || ext == "png" || ext == "jpg" || ext == "jpeg" || ext == "gif" || ext == "ppm";
        if (path.empty() || !underRoot || !isImage || path.find("..") != std::string::npos) {
            return jsonResponse(req, "{\"error\":\"invalid image path\"}", http::status::bad_request);
        }
        const std::vector<unsigned char> bytes = readFileBytes(path);
        if (bytes.empty()) {
            return jsonResponse(req, "{\"error\":\"image file not found\"}", http::status::not_found);
        }
        return binaryFileResponse(req, bytes, contentTypeForImagePath(path));
    }

    http::response<http::string_body> packageOrder(const http::request<http::string_body>& req)
    {
        const int patientId = jsonIntValue(req.body(), "patientId");
        const int requestedOrderId = jsonIntValue(req.body(), "orderId");
        const int orderId = requestedOrderId > 0 ? requestedOrderId : database_.latestOrderId(patientId);
        const Patient patient = database_.patientByOrderId(orderId);
        const std::string orderNo = database_.orderNo(orderId);
        if (patient.id == 0 || orderNo.empty()) {
            return jsonResponse(req, "{\"error\":\"invalid order\"}", http::status::bad_request);
        }

        std::vector<std::string> files = database_.latestImagePathsForOrder(orderId, 24);
        const std::string plyPath = database_.latestPlyPathForOrder(orderId);
        if (!plyPath.empty()) {
            files.push_back(plyPath);
        }
        if (files.empty()) {
            return jsonResponse(req, "{\"error\":\"no images or point cloud files to package\"}", http::status::bad_request);
        }

        const std::string safeOrderName = orderFolderName(orderNo);
        const std::string folder = orderDirectory(imageRoot_, patient, orderNo);
        const std::string previewPath = database_.latestPreviewImagePathForOrder(orderId);
        if (!readFileBytes(previewPath).empty()) {
            files.push_back(previewPath);
        }
        const std::string zipPath = folder + "/" + safeOrderName + "_package.zip";
        if (!writeZipDeflate(zipPath, files)) {
            return jsonResponse(req, "{\"error\":\"package failed\"}", http::status::internal_server_error);
        }
        return jsonResponse(req, "{\"ok\":true,\"message\":\"订单数据已打包并替换旧压缩文件\",\"path\":\"" + escapeJson(zipPath) + "\"}");
    }
};

App::App(const AppConfig& config)
    : impl_(new Impl(config))
{
}

App::~App()
{
}

http::response<http::string_body> App::handle(const http::request<http::string_body>& req)
{
    return impl_->handle(req);
}

} // namespace facescan
