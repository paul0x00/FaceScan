#pragma once

#include <map>
#include <string>

namespace facescan {

std::string escapeJson(const std::string& value);
std::string jsonPair(const std::string& key, const std::string& value);
std::string jsonPair(const std::string& key, int value);
std::string jsonStringValue(const std::string& body, const std::string& key);
int jsonIntValue(const std::string& body, const std::string& key);
std::string urlDecode(const std::string& value);
std::map<std::string, std::string> parseQuery(const std::string& target);
std::string pathOnly(const std::string& target);

} // namespace facescan
