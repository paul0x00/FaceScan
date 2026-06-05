#include "../src/common/json_utils.hpp"

#include <gtest/gtest.h>

using namespace facescan;

/// 验证 JSON 字符串转义。
TEST(JsonUtilsTest, EscapesStringsForJson)
{
    EXPECT_EQ("a\\\"b\\\\c\\n", escapeJson("a\"b\\c\n"));
}

/// 验证轻量 JSON 字段读取。
TEST(JsonUtilsTest, ReadsSimpleStringAndIntegerFields)
{
    const std::string body = "{\"name\":\"Alice\",\"age\":32,\"remark\":\"line\\nnext\"}";

    EXPECT_EQ("Alice", jsonStringValue(body, "name"));
    EXPECT_EQ(32, jsonIntValue(body, "age"));
    EXPECT_EQ("line\nnext", jsonStringValue(body, "remark"));
    EXPECT_EQ("", jsonStringValue(body, "missing"));
    EXPECT_EQ(0, jsonIntValue(body, "missing"));
}

/// 验证轻量 JSON 布尔字段读取和字段存在判断。
TEST(JsonUtilsTest, ReadsSimpleBooleanFieldsAndKeyPresence)
{
    const std::string body = "{\"autoExposure\":false,\"enabled\":true,\"numeric\":1}";

    EXPECT_TRUE(jsonHasKey(body, "autoExposure"));
    EXPECT_TRUE(jsonHasKey(body, "enabled"));
    EXPECT_FALSE(jsonHasKey(body, "missing"));
    EXPECT_FALSE(jsonBoolValue(body, "autoExposure"));
    EXPECT_TRUE(jsonBoolValue(body, "enabled"));
    EXPECT_TRUE(jsonBoolValue(body, "numeric"));
    EXPECT_FALSE(jsonBoolValue(body, "missing"));
}

/// 验证 URL 查询串和路径解析。
TEST(JsonUtilsTest, ParsesUrlQueryAndPath)
{
    const std::string target = "/api/patients?keyword=%E6%B5%8B%E8%AF%95&date=2026-05-26&empty=";
    const std::map<std::string, std::string> query = parseQuery(target);

    EXPECT_EQ("/api/patients", pathOnly(target));
    ASSERT_EQ(3u, query.size());
    EXPECT_EQ("测试", query.at("keyword"));
    EXPECT_EQ("2026-05-26", query.at("date"));
    EXPECT_EQ("", query.at("empty"));
}
