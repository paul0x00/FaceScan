#include "common/json_utils.hpp"

#include <cctype>
#include <cstdlib>
#include <iomanip>
#include <sstream>

namespace facescan {

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

std::string jsonPair(const std::string& key, const std::string& value)
{
    return "\"" + key + "\":\"" + escapeJson(value) + "\"";
}

std::string jsonPair(const std::string& key, int value)
{
    std::ostringstream os;
    os << "\"" << key << "\":" << value;
    return os.str();
}

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
    return std::atoi(body.c_str() + pos);
}

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

std::string pathOnly(const std::string& target)
{
    const std::size_t pos = target.find('?');
    return pos == std::string::npos ? target : target.substr(0, pos);
}

} // namespace facescan
