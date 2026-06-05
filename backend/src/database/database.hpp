#pragma once

#include "common/models.hpp"

#include <sqlite3.h>

#include <mutex>
#include <string>
#include <vector>

namespace facescan {

/// SQLite 数据访问层，负责患者、订单、扫描记录和数据目录索引同步。
class Database {
public:
    /// 打开或创建 SQLite 数据库，并完成建表、迁移和初始数据准备。
    explicit Database(const std::string& filePath);
    /// 关闭 SQLite 连接。
    ~Database();

    /// 按关键字和创建日期查询患者列表。
    std::vector<Patient> patients(const std::string& keyword, const std::string& date);
    /// 根据主键读取患者详情；未找到时返回 id 为 0 的对象。
    Patient patientById(int id);
    /// 创建患者并自动创建首个订单，返回患者主键。
    int createPatient(const Patient& patient);
    /// 删除患者及其关联订单、扫描记录。
    bool deletePatient(int id);
    /// 更新患者基础资料。
    bool updatePatient(int id, const Patient& patient);
    /// 为患者创建新订单并返回订单主键。
    int createOrder(int patientId);
    /// 删除指定订单及其扫描记录。
    bool deleteOrder(int orderId);
    /// 根据订单主键读取所属患者。
    Patient patientByOrderId(int orderId);
    /// 根据订单主键读取订单编号。
    std::string orderNo(int orderId);
    /// 将图片根目录中的患者/订单文件索引同步到数据库。
    void syncDataRoot(const std::vector<DataRootPatient>& patients);
    /// 读取订单最近可展示的预览路径。
    std::string latestPreviewPathForOrder(int orderId);
    /// 读取订单最近一张采集图片预览路径。
    std::string latestPreviewImagePathForOrder(int orderId);
    /// 读取订单最近生成的 PLY 点云路径。
    std::string latestPlyPathForOrder(int orderId);
    /// 读取订单最近采集的图片路径，limit 控制最多返回数量。
    std::vector<std::string> latestImagePathsForOrder(int orderId, int limit);
    /// 查询患者全部订单摘要。
    std::vector<Order> ordersForPatient(int patientId);
    /// 查询患者最近订单主键；不存在时自动创建一个。
    int latestOrderId(int patientId);
    /// 用新的采集图片集合替换订单扫描记录。
    void replaceOrderCapture(int orderId, const std::vector<std::string>& imagePaths);
    /// 写入订单点云结果并更新预览路径。
    void setPointCloudResult(int orderId, const std::string& plyPath, const std::string& previewPath);
    /// 数据目录迁移后批量替换已存储路径前缀。
    void replaceStoredPathPrefix(const std::string& oldPrefix, const std::string& newPrefix);
    /// 查询患者全部扫描记录。
    std::vector<ScanResult> scansForPatient(int patientId);

private:
    /// SQLite 原生连接句柄，由本类独占管理。
    sqlite3* db_;
    /// 串行化数据库访问，避免 HTTP 多线程并发写冲突。
    std::mutex mutex_;

    /// 执行不返回结果集的 SQL，失败时抛出异常。
    void exec(const std::string& sql);
    /// 执行兼容性迁移 SQL，失败时静默忽略。
    void execIgnoreError(const std::string& sql);
    /// 表缺少字段时补充字段，用于轻量迁移。
    void addColumnIfMissing(const std::string& table, const std::string& column, const std::string& type);
    /// 预编译 SQL 语句，失败时抛出异常。
    sqlite3_stmt* prepare(const std::string& sql);
    /// 执行语句并要求结果为 SQLITE_DONE。
    void stepDone(sqlite3_stmt* stmt);

    /// 安全读取 SQLite 文本列。
    static std::string columnText(sqlite3_stmt* stmt, int column);
    /// 从当前结果行映射患者对象。
    static Patient readPatient(sqlite3_stmt* stmt);
    /// 从换行分隔的采图路径中选择正面图。
    static std::string frontImagePath(const std::string& imagePathText);
    /// 将患者字段绑定到 prepared statement。
    static void bindPatientFields(sqlite3_stmt* stmt, const Patient& patient);
    /// 将整数转换为字符串。
    static std::string toString(int value);
    /// 将路径集合编码为换行文本。
    static std::string joinLines(const std::vector<std::string>& values);
    /// 将换行文本解码为路径集合。
    static std::vector<std::string> splitLines(const std::string& value);

    /// 无锁插入或更新数据目录中的患者索引。
    int upsertPatientUnlocked(const DataRootPatient& patient);
    /// 无锁插入或更新数据目录中的订单索引。
    int upsertOrderUnlocked(int patientId, const DataRootOrder& order);
    /// 无锁替换订单对应的扫描记录。
    void replaceOrderScanUnlocked(int orderId, const DataRootOrder& order);
    /// 无锁创建订单。
    int createOrderUnlocked(int patientId);
    /// 无锁读取患者编号。
    std::string patientNoUnlocked(int patientId);
    /// 无锁计算患者的下一个订单序号。
    int nextOrderSequenceUnlocked(int patientId);
    /// 无锁生成新的患者编号。
    std::string generatePatientNoUnlocked();
    /// 清理失去患者或订单引用的冗余记录。
    void cleanupOrphansUnlocked();
    /// 首次运行时写入演示数据。
    void seed();
};

} // namespace facescan
