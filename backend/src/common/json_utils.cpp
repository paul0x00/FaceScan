#include "json_utils.hpp"

#include <cctype>
#include <cstdlib>
#include <iomanip>
#include <limits>
#include <sstream>

namespace facescan {

/// 转义字符串为 JSON 字符串安全内容。
std::string escapeJson(const std::string& value)
{
    std::ostringstream os;
    for (std::string::const_iterator it = value.begin(); it != value.end(); ++it) {
        const char c = *it;
        switch (c) {
        case '"': os << "\\\""; break;
        case '\\': os << "\\\\"; break;
        case '\b': os << "\\b"; break;
        case '\f': os << "\\f"; break;
        case '\n': os << "\\n"; break;
        case '\r': os << "\\r"; break;
        case '\t': os << "\\t"; break;
        default:
            if (static_cast<unsigned char>(c) < 0x20) {
                os << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<int>(c);
            } else {
                os << c;
            }
        }
    }
    return os.str();
}

/// 构造字符串 JSON 键值片段。
std::string jsonPair(const std::string& key, const std::string& value)
{
    return "\"" + key + "\":\"" + escapeJson(value) + "\"";
}

/// 构造整数 JSON 键值片段。
std::string jsonPair(const std::string& key, int value)
{
    std::ostringstream os;
    os << "\"" << key << "\":" << value;
    return os.str();
}

/// 从简单 JSON 文本中查找并解析字符串字段。
std::string jsonStringValue(const std::string& body, const std::string& key)
{
    const std::string needle = "\"" + key + "\"";
    std::size_t pos = body.find(needle);
    if (pos == std::string::npos) {
        return "";
    }
    pos = body.find(':', pos + needle.size());
    if (pos == std::string::npos) {
        return "";
    }
    pos = body.find('"', pos + 1);
    if (pos == std::string::npos) {
        return "";
    }
    std::string out;
    bool escape = false;
    for (++pos; pos < body.size(); ++pos) {
        char c = body[pos];
        if (escape) {
            switch (c) {
            case 'n': out.push_back('\n'); break;
            case 'r': out.push_back('\r'); break;
            case 't': out.push_back('\t'); break;
            default: out.push_back(c); break;
            }
            escape = false;
        } else if (c == '\\') {
            escape = true;
        } else if (c == '"') {
            break;
        } else {
            out.push_back(c);
        }
    }
    return out;
}

/// 从简单 JSON 文本中查找并解析整数字段。
int jsonIntValue(const std::string& body, const std::string& key)
{
    const std::string needle = "\"" + key + "\"";
    std::size_t pos = body.find(needle);
    if (pos == std::string::npos) {
        return 0;
    }
    pos = body.find(':', pos + needle.size());
    if (pos == std::string::npos) {
        return 0;
    }
    ++pos;
    while (pos < body.size() && std::isspace(static_cast<unsigned char>(body[pos]))) {
        ++pos;
    }
    char* end = NULL;
    const long value = std::strtol(body.c_str() + pos, &end, 10);
    if (!end || end == body.c_str() + pos
        || value < std::numeric_limits<int>::min()
        || value > std::numeric_limits<int>::max()) {
        return 0;
    }
    return static_cast<int>(value);
}

/// 从简单 JSON 文本中查找并解析整数数组字段。
std::vector<int> jsonIntArrayValue(const std::string& body, const std::string& key)
{
    std::vector<int> values;
    const std::string needle = "\"" + key + "\"";
    std::size_t pos = body.find(needle);
    if (pos == std::string::npos) {
        return values;
    }
    pos = body.find(':', pos + needle.size());
    if (pos == std::string::npos) {
        return values;
    }
    ++pos;
    while (pos < body.size() && std::isspace(static_cast<unsigned char>(body[pos]))) {
        ++pos;
    }
    if (pos >= body.size() || body[pos] != '[') {
        return values;
    }
    ++pos;
    while (pos < body.size()) {
        while (pos < body.size() && std::isspace(static_cast<unsigned char>(body[pos]))) {
            ++pos;
        }
        if (pos < body.size() && body[pos] == ']') {
            return values;
        }

        char* end = NULL;
        const long value = std::strtol(body.c_str() + pos, &end, 10);
        if (!end || end == body.c_str() + pos
            || value < std::numeric_limits<int>::min()
            || value > std::numeric_limits<int>::max()) {
            return std::vector<int>();
        }
        values.push_back(static_cast<int>(value));
        pos = static_cast<std::size_t>(end - body.c_str());
        while (pos < body.size() && std::isspace(static_cast<unsigned char>(body[pos]))) {
            ++pos;
        }
        if (pos < body.size() && body[pos] == ']') {
            return values;
        }
        if (pos >= body.size() || body[pos] != ',') {
            return std::vector<int>();
        }
        ++pos;
    }
    return std::vector<int>();
}

/// 判断简单 JSON 文本中是否包含字段。
bool jsonHasKey(const std::string& body, const std::string& key)
{
    return body.find("\"" + key + "\"") != std::string::npos;
}

/// 从简单 JSON 文本中查找并解析布尔字段。
bool jsonBoolValue(const std::string& body, const std::string& key)
{
    const std::string needle = "\"" + key + "\"";
    std::size_t pos = body.find(needle);
    if (pos == std::string::npos) {
        return false;
    }
    pos = body.find(':', pos + needle.size());
    if (pos == std::string::npos) {
        return false;
    }
    ++pos;
    while (pos < body.size() && std::isspace(static_cast<unsigned char>(body[pos]))) {
        ++pos;
    }
    return body.compare(pos, 4, "true") == 0 || body.compare(pos, 1, "1") == 0;
}

/// 解码查询串中的 percent-encoding 和加号空格。
std::string urlDecode(const std::string& value)
{
    std::string out;
    for (std::size_t i = 0; i < value.size(); ++i) {
        if (value[i] == '%' && i + 2 < value.size()) {
            const std::string hex = value.substr(i + 1, 2);
            char* end = NULL;
            const long code = std::strtol(hex.c_str(), &end, 16);
            if (end && *end == '\0') {
                out.push_back(static_cast<char>(code));
                i += 2;
            }
        } else if (value[i] == '+') {
            out.push_back(' ');
        } else {
            out.push_back(value[i]);
        }
    }
    return out;
}

/// 解析 URL target 中的查询参数。
std::map<std::string, std::string> parseQuery(const std::string& target)
{
    std::map<std::string, std::string> query;
    const std::size_t pos = target.find('?');
    if (pos == std::string::npos) {
        return query;
    }
    std::stringstream ss(target.substr(pos + 1));
    std::string item;
    while (std::getline(ss, item, '&')) {
        const std::size_t eq = item.find('=');
        if (eq == std::string::npos) {
            query[urlDecode(item)] = "";
        } else {
            query[urlDecode(item.substr(0, eq))] = urlDecode(item.substr(eq + 1));
        }
    }
    return query;
}

/// 移除 URL target 的查询串部分。
std::string pathOnly(const std::string& target)
{
    const std::size_t pos = target.find('?');
    return pos == std::string::npos ? target : target.substr(0, pos);
}

} // namespace facescan
