#pragma once

#include <map>
#include <string>

namespace facescan {

/// 转义字符串为 JSON 字符串字面量内容。
std::string escapeJson(const std::string& value);
/// 构造字符串字段的 JSON 键值片段。
std::string jsonPair(const std::string& key, const std::string& value);
/// 构造整数字段的 JSON 键值片段。
std::string jsonPair(const std::string& key, int value);
/// 从简单 JSON 请求体中读取字符串字段。
std::string jsonStringValue(const std::string& body, const std::string& key);
/// 从简单 JSON 请求体中读取整数字段。
int jsonIntValue(const std::string& body, const std::string& key);
/// 判断简单 JSON 请求体是否包含指定字段。
bool jsonHasKey(const std::string& body, const std::string& key);
/// 从简单 JSON 请求体中读取布尔字段。
bool jsonBoolValue(const std::string& body, const std::string& key);
/// 解码 URL 查询参数中的百分号编码。
std::string urlDecode(const std::string& value);
/// 解析 URL target 中的查询参数。
std::map<std::string, std::string> parseQuery(const std::string& target);
/// 去掉 URL target 中的查询串，仅保留路径。
std::string pathOnly(const std::string& target);

} // namespace facescan
