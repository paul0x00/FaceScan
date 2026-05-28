#include "database.hpp"

#include "../common/file_utils.hpp"
#include "../common/time_utils.hpp"

#include <algorithm>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>

namespace facescan {

/// 打开数据库并确保基础表、轻量迁移和种子数据就绪。
Database::Database(const std::string& filePath) : db_(NULL)
{
    ensureDirectory(parentDirectory(filePath));
    if (sqlite3_open(filePath.c_str(), &db_) != SQLITE_OK) {
        throw std::runtime_error("Cannot open SQLite database");
    }
    exec("PRAGMA foreign_keys=ON;");
    exec("PRAGMA journal_mode=WAL;");
    exec("CREATE TABLE IF NOT EXISTS patient ("
         "id INTEGER PRIMARY KEY AUTOINCREMENT,"
         "patient_no TEXT NOT NULL,"
         "name TEXT NOT NULL,"
         "gender TEXT,"
         "age INTEGER DEFAULT 0,"
         "phone TEXT,"
         "doctor TEXT,"
         "remark TEXT,"
         "created_at TEXT NOT NULL"
         ");");
    exec("CREATE TABLE IF NOT EXISTS orders ("
         "id INTEGER PRIMARY KEY AUTOINCREMENT,"
         "patient_id INTEGER NOT NULL,"
         "order_no TEXT,"
         "status TEXT NOT NULL,"
         "created_at TEXT NOT NULL,"
         "FOREIGN KEY(patient_id) REFERENCES patient(id)"
         ");");
    addColumnIfMissing("orders", "order_no", "TEXT");
    exec("UPDATE orders SET order_no=(SELECT patient_no FROM patient WHERE patient.id=orders.patient_id)||'-'||id "
         "WHERE order_no IS NULL OR order_no='';");
    exec("CREATE TABLE IF NOT EXISTS scan_result ("
         "id INTEGER PRIMARY KEY AUTOINCREMENT,"
         "order_id INTEGER NOT NULL,"
         "ply_path TEXT,"
         "image_paths TEXT,"
         "preview_path TEXT NOT NULL,"
         "created_at TEXT NOT NULL,"
         "FOREIGN KEY(order_id) REFERENCES orders(id)"
         ");");
    addColumnIfMissing("scan_result", "image_paths", "TEXT");
    seed();
}

/// 关闭 SQLite 连接。
Database::~Database()
{
    if (db_) {
        sqlite3_close(db_);
    }
}

/// 按关键字和日期筛选患者，并带出最近订单编号。
std::vector<Patient> Database::patients(const std::string& keyword, const std::string& date)
{
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<Patient> out;
    std::string sql = "SELECT id, patient_no, "
        "(SELECT order_no FROM orders WHERE orders.patient_id=patient.id ORDER BY id DESC LIMIT 1) AS order_no, "
        "name, gender, age, phone, doctor, remark, created_at FROM patient";
    std::vector<std::string> params;
    std::vector<std::string> clauses;
    if (!keyword.empty()) {
        clauses.push_back("(patient_no LIKE ? OR name LIKE ? OR phone LIKE ? OR doctor LIKE ?)");
        const std::string like = "%" + keyword + "%";
        params.push_back(like);
        params.push_back(like);
        params.push_back(like);
        params.push_back(like);
    }
    if (!date.empty()) {
        clauses.push_back("date(created_at)=date(?)");
        params.push_back(date);
    }
    if (!clauses.empty()) {
        sql += " WHERE ";
        for (std::size_t i = 0; i < clauses.size(); ++i) {
            if (i) sql += " AND ";
            sql += clauses[i];
        }
    }
    sql += " ORDER BY id DESC";
    sqlite3_stmt* stmt = prepare(sql);
    for (std::size_t i = 0; i < params.size(); ++i) {
        sqlite3_bind_text(stmt, static_cast<int>(i + 1), params[i].c_str(), -1, SQLITE_TRANSIENT);
    }
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        out.push_back(readPatient(stmt));
    }
    sqlite3_finalize(stmt);
    return out;
}

/// 根据患者主键读取完整患者信息。
Patient Database::patientById(int id)
{
    std::lock_guard<std::mutex> lock(mutex_);
    sqlite3_stmt* stmt = prepare("SELECT id, patient_no, "
        "(SELECT order_no FROM orders WHERE orders.patient_id=patient.id ORDER BY id DESC LIMIT 1) AS order_no, "
        "name, gender, age, phone, doctor, remark, created_at FROM patient WHERE id=?");
    sqlite3_bind_int(stmt, 1, id);
    Patient p;
    p.id = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        p = readPatient(stmt);
    }
    sqlite3_finalize(stmt);
    return p;
}

/// 创建患者并自动创建首个订单。
int Database::createPatient(const Patient& patient)
{
    std::lock_guard<std::mutex> lock(mutex_);
    Patient generated = patient;
    generated.patientNo = generatePatientNoUnlocked();
    sqlite3_stmt* stmt = prepare("INSERT INTO patient(patient_no,name,gender,age,phone,doctor,remark,created_at) VALUES(?,?,?,?,?,?,?,?)");
    bindPatientFields(stmt, generated);
    stepDone(stmt);
    sqlite3_finalize(stmt);
    const int id = static_cast<int>(sqlite3_last_insert_rowid(db_));
    createOrderUnlocked(id);
    return id;
}

/// 删除患者及其关联订单、扫描记录。
bool Database::deletePatient(int id)
{
    std::lock_guard<std::mutex> lock(mutex_);
    exec("BEGIN IMMEDIATE;");
    sqlite3_stmt* scanStmt = prepare("DELETE FROM scan_result WHERE order_id IN (SELECT id FROM orders WHERE patient_id=?)");
    sqlite3_bind_int(scanStmt, 1, id);
    stepDone(scanStmt);
    sqlite3_finalize(scanStmt);

    sqlite3_stmt* orderStmt = prepare("DELETE FROM orders WHERE patient_id=?");
    sqlite3_bind_int(orderStmt, 1, id);
    stepDone(orderStmt);
    sqlite3_finalize(orderStmt);

    sqlite3_stmt* patientStmt = prepare("DELETE FROM patient WHERE id=?");
    sqlite3_bind_int(patientStmt, 1, id);
    stepDone(patientStmt);
    const bool changed = sqlite3_changes(db_) > 0;
    sqlite3_finalize(patientStmt);
    cleanupOrphansUnlocked();
    exec("COMMIT;");
    execIgnoreError("PRAGMA optimize;");
    return changed;
}

/// 更新患者可编辑字段。
bool Database::updatePatient(int id, const Patient& patient)
{
    std::lock_guard<std::mutex> lock(mutex_);
    sqlite3_stmt* stmt = prepare("UPDATE patient SET name=?, gender=?, age=?, phone=?, doctor=?, remark=? WHERE id=?");
    sqlite3_bind_text(stmt, 1, patient.name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, patient.gender.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 3, patient.age);
    sqlite3_bind_text(stmt, 4, patient.phone.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, patient.doctor.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 6, patient.remark.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 7, id);
    stepDone(stmt);
    const bool changed = sqlite3_changes(db_) > 0;
    sqlite3_finalize(stmt);
    return changed;
}

/// 为患者创建新订单。
int Database::createOrder(int patientId)
{
    std::lock_guard<std::mutex> lock(mutex_);
    return createOrderUnlocked(patientId);
}

/// 删除订单及关联扫描记录。
bool Database::deleteOrder(int orderId)
{
    std::lock_guard<std::mutex> lock(mutex_);
    exec("BEGIN IMMEDIATE;");
    sqlite3_stmt* scanStmt = prepare("DELETE FROM scan_result WHERE order_id=?");
    sqlite3_bind_int(scanStmt, 1, orderId);
    stepDone(scanStmt);
    sqlite3_finalize(scanStmt);

    sqlite3_stmt* stmt = prepare("DELETE FROM orders WHERE id=?");
    sqlite3_bind_int(stmt, 1, orderId);
    stepDone(stmt);
    const bool changed = sqlite3_changes(db_) > 0;
    sqlite3_finalize(stmt);
    cleanupOrphansUnlocked();
    exec("COMMIT;");
    execIgnoreError("PRAGMA optimize;");
    return changed;
}

/// 根据订单查找所属患者。
Patient Database::patientByOrderId(int orderId)
{
    std::lock_guard<std::mutex> lock(mutex_);
    sqlite3_stmt* stmt = prepare("SELECT patient.id, patient.patient_no, orders.order_no, "
        "patient.name, patient.gender, patient.age, patient.phone, patient.doctor, patient.remark, patient.created_at "
        "FROM patient JOIN orders ON orders.patient_id=patient.id WHERE orders.id=?");
    sqlite3_bind_int(stmt, 1, orderId);
    Patient p;
    p.id = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        p = readPatient(stmt);
    }
    sqlite3_finalize(stmt);
    return p;
}

/// 读取订单业务编号。
std::string Database::orderNo(int orderId)
{
    std::lock_guard<std::mutex> lock(mutex_);
    sqlite3_stmt* stmt = prepare("SELECT order_no FROM orders WHERE id=?");
    sqlite3_bind_int(stmt, 1, orderId);
    std::string value;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        value = columnText(stmt, 0);
    }
    sqlite3_finalize(stmt);
    return value;
}

/// 将文件系统扫描结果同步到数据库，删除已不存在的患者和订单。
void Database::syncDataRoot(const std::vector<DataRootPatient>& patients)
{
    std::lock_guard<std::mutex> lock(mutex_);
    try {
        exec("BEGIN IMMEDIATE;");

        std::set<std::string> scannedPatientNos;
        std::map<std::string, std::set<std::string> > scannedOrderNos;
        for (std::size_t i = 0; i < patients.size(); ++i) {
            if (patients[i].patientNo.empty()) {
                continue;
            }
            scannedPatientNos.insert(patients[i].patientNo);
            std::set<std::string>& orderSet = scannedOrderNos[patients[i].patientNo];
            for (std::size_t j = 0; j < patients[i].orders.size(); ++j) {
                if (!patients[i].orders[j].orderNo.empty()) {
                    orderSet.insert(patients[i].orders[j].orderNo);
                }
            }
        }

        std::vector<int> missingPatientIds;
        sqlite3_stmt* patientStmt = prepare("SELECT id, patient_no FROM patient");
        while (sqlite3_step(patientStmt) == SQLITE_ROW) {
            const int id = sqlite3_column_int(patientStmt, 0);
            const std::string patientNo = columnText(patientStmt, 1);
            if (scannedPatientNos.find(patientNo) == scannedPatientNos.end()) {
                missingPatientIds.push_back(id);
            }
        }
        sqlite3_finalize(patientStmt);

        for (std::size_t i = 0; i < missingPatientIds.size(); ++i) {
            sqlite3_stmt* scanStmt = prepare("DELETE FROM scan_result WHERE order_id IN (SELECT id FROM orders WHERE patient_id=?)");
            sqlite3_bind_int(scanStmt, 1, missingPatientIds[i]);
            stepDone(scanStmt);
            sqlite3_finalize(scanStmt);

            sqlite3_stmt* orderStmt = prepare("DELETE FROM orders WHERE patient_id=?");
            sqlite3_bind_int(orderStmt, 1, missingPatientIds[i]);
            stepDone(orderStmt);
            sqlite3_finalize(orderStmt);

            sqlite3_stmt* deletePatientStmt = prepare("DELETE FROM patient WHERE id=?");
            sqlite3_bind_int(deletePatientStmt, 1, missingPatientIds[i]);
            stepDone(deletePatientStmt);
            sqlite3_finalize(deletePatientStmt);
        }

        for (std::size_t i = 0; i < patients.size(); ++i) {
            if (patients[i].patientNo.empty()) {
                continue;
            }
            const int patientId = upsertPatientUnlocked(patients[i]);
            const std::set<std::string>& orderSet = scannedOrderNos[patients[i].patientNo];

            std::vector<int> missingOrderIds;
            sqlite3_stmt* existingOrderStmt = prepare("SELECT id, order_no FROM orders WHERE patient_id=?");
            sqlite3_bind_int(existingOrderStmt, 1, patientId);
            while (sqlite3_step(existingOrderStmt) == SQLITE_ROW) {
                const int id = sqlite3_column_int(existingOrderStmt, 0);
                const std::string orderNo = columnText(existingOrderStmt, 1);
                if (orderSet.find(orderNo) == orderSet.end()) {
                    missingOrderIds.push_back(id);
                }
            }
            sqlite3_finalize(existingOrderStmt);

            for (std::size_t j = 0; j < missingOrderIds.size(); ++j) {
                sqlite3_stmt* deleteScanStmt = prepare("DELETE FROM scan_result WHERE order_id=?");
                sqlite3_bind_int(deleteScanStmt, 1, missingOrderIds[j]);
                stepDone(deleteScanStmt);
                sqlite3_finalize(deleteScanStmt);

                sqlite3_stmt* deleteOrderStmt = prepare("DELETE FROM orders WHERE id=?");
                sqlite3_bind_int(deleteOrderStmt, 1, missingOrderIds[j]);
                stepDone(deleteOrderStmt);
                sqlite3_finalize(deleteOrderStmt);
            }

            for (std::size_t j = 0; j < patients[i].orders.size(); ++j) {
                if (patients[i].orders[j].orderNo.empty()) {
                    continue;
                }
                const int orderId = upsertOrderUnlocked(patientId, patients[i].orders[j]);
                replaceOrderScanUnlocked(orderId, patients[i].orders[j]);
            }
        }

        cleanupOrphansUnlocked();
        exec("COMMIT;");
        execIgnoreError("PRAGMA optimize;");
    } catch (...) {
        execIgnoreError("ROLLBACK;");
        throw;
    }
}

/// 查询订单最近一条可用预览路径，优先返回 PLY 路径。
std::string Database::latestPreviewPathForOrder(int orderId)
{
    std::lock_guard<std::mutex> lock(mutex_);
    sqlite3_stmt* stmt = prepare(
        "SELECT CASE WHEN ply_path IS NOT NULL AND ply_path<>'' THEN ply_path ELSE preview_path END "
        "FROM scan_result WHERE order_id=? ORDER BY id DESC LIMIT 1");
    sqlite3_bind_int(stmt, 1, orderId);
    std::string value;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        value = columnText(stmt, 0);
    }
    sqlite3_finalize(stmt);
    return value;
}

/// 查询订单最近生成的 PLY 文件路径。
std::string Database::latestPlyPathForOrder(int orderId)
{
    std::lock_guard<std::mutex> lock(mutex_);
    sqlite3_stmt* stmt = prepare(
        "SELECT ply_path FROM scan_result "
        "WHERE order_id=? AND ply_path IS NOT NULL AND ply_path<>'' "
        "ORDER BY id DESC LIMIT 1");
    sqlite3_bind_int(stmt, 1, orderId);
    std::string value;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        value = columnText(stmt, 0);
    }
    sqlite3_finalize(stmt);
    return value;
}

/// 查询订单最近的图片预览路径。
std::string Database::latestPreviewImagePathForOrder(int orderId)
{
    std::lock_guard<std::mutex> lock(mutex_);
    sqlite3_stmt* stmt = prepare(
        "SELECT preview_path FROM scan_result "
        "WHERE order_id=? AND preview_path IS NOT NULL AND preview_path<>'' "
        "ORDER BY id DESC LIMIT 1");
    sqlite3_bind_int(stmt, 1, orderId);
    std::string value;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        value = columnText(stmt, 0);
    }
    sqlite3_finalize(stmt);
    return value;
}

/// 查询订单最近一次采集的图片路径集合。
std::vector<std::string> Database::latestImagePathsForOrder(int orderId, int limit)
{
    std::lock_guard<std::mutex> lock(mutex_);
    const int cappedLimit = limit <= 0 ? 4 : std::min(limit, 24);
    sqlite3_stmt* groupedStmt = prepare(
        "SELECT image_paths FROM scan_result "
        "WHERE order_id=? AND image_paths IS NOT NULL AND image_paths<>'' "
        "ORDER BY id DESC LIMIT 1");
    sqlite3_bind_int(groupedStmt, 1, orderId);
    std::vector<std::string> groupedPaths;
    if (sqlite3_step(groupedStmt) == SQLITE_ROW) {
        groupedPaths = splitLines(columnText(groupedStmt, 0));
    }
    sqlite3_finalize(groupedStmt);
    if (!groupedPaths.empty()) {
        if (static_cast<int>(groupedPaths.size()) > cappedLimit) {
            groupedPaths.resize(static_cast<std::size_t>(cappedLimit));
        }
        return groupedPaths;
    }

    sqlite3_stmt* stmt = prepare(
        "SELECT preview_path FROM scan_result "
        "WHERE order_id=? AND preview_path<>'' AND (ply_path IS NULL OR ply_path='') "
        "ORDER BY id DESC LIMIT ?");
    sqlite3_bind_int(stmt, 1, orderId);
    sqlite3_bind_int(stmt, 2, cappedLimit);
    std::vector<std::string> paths;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const std::string path = columnText(stmt, 0);
        if (path.find(".ply") == std::string::npos) {
            paths.push_back(path);
        }
    }
    sqlite3_finalize(stmt);
    std::reverse(paths.begin(), paths.end());
    return paths;
}

/// 查询患者订单摘要，包含扫描数量、预览和点云路径。
std::vector<Order> Database::ordersForPatient(int patientId)
{
    std::lock_guard<std::mutex> lock(mutex_);
    sqlite3_stmt* stmt = prepare(
        "SELECT orders.id, orders.patient_id, orders.order_no, orders.status, orders.created_at, "
        "COUNT(scan_result.id) AS scan_count, "
        "(SELECT preview_path FROM scan_result WHERE scan_result.order_id=orders.id ORDER BY id DESC LIMIT 1) AS preview_path, "
        "(SELECT ply_path FROM scan_result WHERE scan_result.order_id=orders.id AND ply_path IS NOT NULL AND ply_path<>'' ORDER BY id DESC LIMIT 1) AS ply_path "
        "FROM orders LEFT JOIN scan_result ON scan_result.order_id=orders.id "
        "WHERE orders.patient_id=? "
        "GROUP BY orders.id, orders.patient_id, orders.order_no, orders.status, orders.created_at "
        "ORDER BY orders.id DESC");
    sqlite3_bind_int(stmt, 1, patientId);
    std::vector<Order> orders;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        Order order;
        order.id = sqlite3_column_int(stmt, 0);
        order.patientId = sqlite3_column_int(stmt, 1);
        order.orderNo = columnText(stmt, 2);
        order.status = columnText(stmt, 3);
        order.createdAt = columnText(stmt, 4);
        order.scanCount = sqlite3_column_int(stmt, 5);
        order.previewPath = columnText(stmt, 6);
        order.plyPath = columnText(stmt, 7);
        orders.push_back(order);
    }
    sqlite3_finalize(stmt);
    return orders;
}

/// 查询患者最近订单；没有订单时自动创建。
int Database::latestOrderId(int patientId)
{
    std::lock_guard<std::mutex> lock(mutex_);
    sqlite3_stmt* stmt = prepare("SELECT id FROM orders WHERE patient_id=? ORDER BY id DESC LIMIT 1");
    sqlite3_bind_int(stmt, 1, patientId);
    int id = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        id = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);
    if (id == 0) {
        id = createOrderUnlocked(patientId);
    }
    return id;
}

/// 用新采图结果替换订单旧扫描记录。
void Database::replaceOrderCapture(int orderId, const std::vector<std::string>& imagePaths)
{
    std::lock_guard<std::mutex> lock(mutex_);
    sqlite3_stmt* deleteStmt = prepare("DELETE FROM scan_result WHERE order_id=?");
    sqlite3_bind_int(deleteStmt, 1, orderId);
    stepDone(deleteStmt);
    sqlite3_finalize(deleteStmt);

    sqlite3_stmt* stmt = prepare("INSERT INTO scan_result(order_id, ply_path, image_paths, preview_path, created_at) VALUES(?,?,?,?,?)");
    const std::string empty;
    const std::string created = nowText();
    const std::string imagePathText = joinLines(imagePaths);
    const std::string previewPath = imagePaths.empty() ? empty : imagePaths[0];
    sqlite3_bind_int(stmt, 1, orderId);
    sqlite3_bind_text(stmt, 2, empty.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, imagePathText.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, previewPath.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, created.c_str(), -1, SQLITE_TRANSIENT);
    stepDone(stmt);
    sqlite3_finalize(stmt);
}

/// 写入或更新订单点云生成结果。
void Database::setPointCloudResult(int orderId, const std::string& plyPath, const std::string& previewPath)
{
    std::lock_guard<std::mutex> lock(mutex_);
    sqlite3_stmt* stmt = prepare(
        "UPDATE scan_result SET ply_path=?, preview_path=CASE WHEN ?<>'' THEN ? ELSE preview_path END, created_at=? WHERE order_id=?");
    const std::string created = nowText();
    sqlite3_bind_text(stmt, 1, plyPath.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, previewPath.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, previewPath.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, created.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 5, orderId);
    stepDone(stmt);
    const bool changed = sqlite3_changes(db_) > 0;
    sqlite3_finalize(stmt);
    if (!changed) {
        sqlite3_stmt* insertStmt = prepare("INSERT INTO scan_result(order_id, ply_path, image_paths, preview_path, created_at) VALUES(?,?,?,?,?)");
        const std::string empty;
        const std::string preview = previewPath.empty() ? plyPath : previewPath;
        sqlite3_bind_int(insertStmt, 1, orderId);
        sqlite3_bind_text(insertStmt, 2, plyPath.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(insertStmt, 3, empty.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(insertStmt, 4, preview.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(insertStmt, 5, created.c_str(), -1, SQLITE_TRANSIENT);
        stepDone(insertStmt);
        sqlite3_finalize(insertStmt);
    }
}

/// 迁移数据目录后替换扫描记录中的路径前缀。
void Database::replaceStoredPathPrefix(const std::string& oldPrefix, const std::string& newPrefix)
{
    if (oldPrefix.empty() || newPrefix.empty() || oldPrefix == newPrefix) {
        return;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    sqlite3_stmt* stmt = prepare(
        "UPDATE scan_result SET "
        "ply_path=CASE WHEN ply_path IS NOT NULL THEN replace(ply_path, ?, ?) ELSE ply_path END,"
        "preview_path=replace(preview_path, ?, ?),"
        "image_paths=CASE WHEN image_paths IS NOT NULL THEN replace(image_paths, ?, ?) ELSE image_paths END "
        "WHERE (ply_path IS NOT NULL AND ply_path LIKE ?) "
        "OR preview_path LIKE ? "
        "OR (image_paths IS NOT NULL AND image_paths LIKE ?)");
    const std::string like = oldPrefix + "%";
    sqlite3_bind_text(stmt, 1, oldPrefix.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, newPrefix.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, oldPrefix.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, newPrefix.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, oldPrefix.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 6, newPrefix.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 7, like.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 8, like.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 9, like.c_str(), -1, SQLITE_TRANSIENT);
    stepDone(stmt);
    sqlite3_finalize(stmt);
}

/// 查询患者所有扫描记录。
std::vector<ScanResult> Database::scansForPatient(int patientId)
{
    std::lock_guard<std::mutex> lock(mutex_);
    sqlite3_stmt* stmt = prepare(
        "SELECT scan_result.id, scan_result.order_id, scan_result.ply_path, scan_result.preview_path, scan_result.image_paths, scan_result.created_at "
        "FROM scan_result JOIN orders ON orders.id=scan_result.order_id "
        "WHERE orders.patient_id=? ORDER BY scan_result.id DESC");
    sqlite3_bind_int(stmt, 1, patientId);
    std::vector<ScanResult> scans;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        ScanResult s;
        s.id = sqlite3_column_int(stmt, 0);
        s.orderId = sqlite3_column_int(stmt, 1);
        s.plyPath = columnText(stmt, 2);
        s.previewPath = columnText(stmt, 3);
        s.imagePaths = splitLines(columnText(stmt, 4));
        s.createdAt = columnText(stmt, 5);
        scans.push_back(s);
    }
    sqlite3_finalize(stmt);
    return scans;
}

/// 执行 SQL 语句并在失败时抛出 SQLite 错误。
void Database::exec(const std::string& sql)
{
    char* err = NULL;
    if (sqlite3_exec(db_, sql.c_str(), NULL, NULL, &err) != SQLITE_OK) {
        const std::string message = err ? err : "SQLite error";
        sqlite3_free(err);
        throw std::runtime_error(message);
    }
}

/// 执行 SQL 语句并忽略失败，适用于兼容性清理和回滚。
void Database::execIgnoreError(const std::string& sql)
{
    char* err = NULL;
    if (sqlite3_exec(db_, sql.c_str(), NULL, NULL, &err) != SQLITE_OK) {
        sqlite3_free(err);
    }
}

/// 如果字段不存在则为表追加字段。
void Database::addColumnIfMissing(const std::string& table, const std::string& column, const std::string& type)
{
    sqlite3_stmt* stmt = prepare("PRAGMA table_info(" + table + ")");
    bool exists = false;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        if (columnText(stmt, 1) == column) {
            exists = true;
            break;
        }
    }
    sqlite3_finalize(stmt);
    if (!exists) {
        execIgnoreError("ALTER TABLE " + table + " ADD COLUMN " + column + " " + type);
    }
}

/// 预编译 SQLite 语句。
sqlite3_stmt* Database::prepare(const std::string& sql)
{
    sqlite3_stmt* stmt = NULL;
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, NULL) != SQLITE_OK) {
        throw std::runtime_error(sqlite3_errmsg(db_));
    }
    return stmt;
}

/// 执行 prepared statement 并要求完成。
void Database::stepDone(sqlite3_stmt* stmt)
{
    if (sqlite3_step(stmt) != SQLITE_DONE) {
        throw std::runtime_error(sqlite3_errmsg(db_));
    }
}

/// 读取 SQLite 文本列，NULL 转为空字符串。
std::string Database::columnText(sqlite3_stmt* stmt, int column)
{
    const unsigned char* text = sqlite3_column_text(stmt, column);
    return text ? reinterpret_cast<const char*>(text) : "";
}

/// 从当前结果行映射患者对象。
Patient Database::readPatient(sqlite3_stmt* stmt)
{
    Patient p;
    p.id = sqlite3_column_int(stmt, 0);
    p.patientNo = columnText(stmt, 1);
    p.orderNo = columnText(stmt, 2);
    p.name = columnText(stmt, 3);
    p.gender = columnText(stmt, 4);
    p.age = sqlite3_column_int(stmt, 5);
    p.phone = columnText(stmt, 6);
    p.doctor = columnText(stmt, 7);
    p.remark = columnText(stmt, 8);
    p.createdAt = columnText(stmt, 9);
    return p;
}

/// 绑定患者插入字段。
void Database::bindPatientFields(sqlite3_stmt* stmt, const Patient& patient)
{
    sqlite3_bind_text(stmt, 1, patient.patientNo.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, patient.name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, patient.gender.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 4, patient.age);
    sqlite3_bind_text(stmt, 5, patient.phone.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 6, patient.doctor.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 7, patient.remark.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 8, patient.createdAt.c_str(), -1, SQLITE_TRANSIENT);
}

/// 将整数转换为字符串。
std::string Database::toString(int value)
{
    std::ostringstream os;
    os << value;
    return os.str();
}

/// 将路径数组编码为换行分隔文本。
std::string Database::joinLines(const std::vector<std::string>& values)
{
    std::ostringstream os;
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i) {
            os << "\n";
        }
        os << values[i];
    }
    return os.str();
}

/// 将换行分隔文本解码为路径数组。
std::vector<std::string> Database::splitLines(const std::string& value)
{
    std::vector<std::string> out;
    std::istringstream input(value);
    std::string line;
    while (std::getline(input, line)) {
        if (!line.empty()) {
            out.push_back(line);
        }
    }
    return out;
}

/// 按患者编号插入或更新由数据目录扫描到的患者。
int Database::upsertPatientUnlocked(const DataRootPatient& patient)
{
    sqlite3_stmt* findStmt = prepare("SELECT id FROM patient WHERE patient_no=?");
    sqlite3_bind_text(findStmt, 1, patient.patientNo.c_str(), -1, SQLITE_TRANSIENT);
    int id = 0;
    if (sqlite3_step(findStmt) == SQLITE_ROW) {
        id = sqlite3_column_int(findStmt, 0);
    }
    sqlite3_finalize(findStmt);

    const std::string name = patient.name.empty() ? patient.patientNo : patient.name;
    const std::string createdAt = patient.createdAt.empty() ? nowText() : patient.createdAt;
    if (id > 0) {
        sqlite3_stmt* updateStmt = prepare(
            "UPDATE patient SET name=?, created_at=CASE WHEN created_at='' THEN ? ELSE created_at END WHERE id=?");
        sqlite3_bind_text(updateStmt, 1, name.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(updateStmt, 2, createdAt.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(updateStmt, 3, id);
        stepDone(updateStmt);
        sqlite3_finalize(updateStmt);
        return id;
    }

    sqlite3_stmt* insertStmt = prepare(
        "INSERT INTO patient(patient_no,name,gender,age,phone,doctor,remark,created_at) VALUES(?,?,?,?,?,?,?,?)");
    const std::string empty;
    sqlite3_bind_text(insertStmt, 1, patient.patientNo.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(insertStmt, 2, name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(insertStmt, 3, empty.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(insertStmt, 4, 0);
    sqlite3_bind_text(insertStmt, 5, empty.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(insertStmt, 6, empty.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(insertStmt, 7, empty.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(insertStmt, 8, createdAt.c_str(), -1, SQLITE_TRANSIENT);
    stepDone(insertStmt);
    sqlite3_finalize(insertStmt);
    return static_cast<int>(sqlite3_last_insert_rowid(db_));
}

/// 按订单编号插入或更新由数据目录扫描到的订单。
int Database::upsertOrderUnlocked(int patientId, const DataRootOrder& order)
{
    sqlite3_stmt* findStmt = prepare("SELECT id FROM orders WHERE patient_id=? AND order_no=?");
    sqlite3_bind_int(findStmt, 1, patientId);
    sqlite3_bind_text(findStmt, 2, order.orderNo.c_str(), -1, SQLITE_TRANSIENT);
    int id = 0;
    if (sqlite3_step(findStmt) == SQLITE_ROW) {
        id = sqlite3_column_int(findStmt, 0);
    }
    sqlite3_finalize(findStmt);

    const std::string createdAt = order.createdAt.empty() ? nowText() : order.createdAt;
    if (id > 0) {
        sqlite3_stmt* updateStmt = prepare(
            "UPDATE orders SET status=?, created_at=CASE WHEN created_at='' THEN ? ELSE created_at END WHERE id=?");
        const std::string status = "synced";
        sqlite3_bind_text(updateStmt, 1, status.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(updateStmt, 2, createdAt.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(updateStmt, 3, id);
        stepDone(updateStmt);
        sqlite3_finalize(updateStmt);
        return id;
    }

    sqlite3_stmt* insertStmt = prepare("INSERT INTO orders(patient_id,order_no,status,created_at) VALUES(?,?,?,?)");
    const std::string status = "synced";
    sqlite3_bind_int(insertStmt, 1, patientId);
    sqlite3_bind_text(insertStmt, 2, order.orderNo.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(insertStmt, 3, status.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(insertStmt, 4, createdAt.c_str(), -1, SQLITE_TRANSIENT);
    stepDone(insertStmt);
    sqlite3_finalize(insertStmt);
    return static_cast<int>(sqlite3_last_insert_rowid(db_));
}

/// 用数据目录扫描结果重建订单扫描记录。
void Database::replaceOrderScanUnlocked(int orderId, const DataRootOrder& order)
{
    sqlite3_stmt* deleteStmt = prepare("DELETE FROM scan_result WHERE order_id=?");
    sqlite3_bind_int(deleteStmt, 1, orderId);
    stepDone(deleteStmt);
    sqlite3_finalize(deleteStmt);

    std::string previewPath = order.previewPath;
    if (previewPath.empty() && !order.imagePaths.empty()) {
        previewPath = order.imagePaths[0];
    }
    if (previewPath.empty()) {
        previewPath = order.plyPath;
    }
    if (previewPath.empty() && order.imagePaths.empty() && order.plyPath.empty()) {
        return;
    }

    sqlite3_stmt* insertStmt = prepare(
        "INSERT INTO scan_result(order_id, ply_path, image_paths, preview_path, created_at) VALUES(?,?,?,?,?)");
    const std::string createdAt = order.createdAt.empty() ? nowText() : order.createdAt;
    const std::string imagePathText = joinLines(order.imagePaths);
    sqlite3_bind_int(insertStmt, 1, orderId);
    sqlite3_bind_text(insertStmt, 2, order.plyPath.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(insertStmt, 3, imagePathText.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(insertStmt, 4, previewPath.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(insertStmt, 5, createdAt.c_str(), -1, SQLITE_TRANSIENT);
    stepDone(insertStmt);
    sqlite3_finalize(insertStmt);
}

/// 创建订单并生成“患者编号-序号”的订单编号。
int Database::createOrderUnlocked(int patientId)
{
    const std::string patientNo = patientNoUnlocked(patientId);
    const int sequence = nextOrderSequenceUnlocked(patientId);
    const std::string orderNo = patientNo + "-" + toString(sequence);
    sqlite3_stmt* stmt = prepare("INSERT INTO orders(patient_id,order_no,status,created_at) VALUES(?,?,?,?)");
    const std::string status = "created";
    const std::string created = nowText();
    sqlite3_bind_int(stmt, 1, patientId);
    sqlite3_bind_text(stmt, 2, orderNo.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, status.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, created.c_str(), -1, SQLITE_TRANSIENT);
    stepDone(stmt);
    sqlite3_finalize(stmt);
    return static_cast<int>(sqlite3_last_insert_rowid(db_));
}

/// 读取患者编号，缺失时生成临时编号兜底。
std::string Database::patientNoUnlocked(int patientId)
{
    sqlite3_stmt* stmt = prepare("SELECT patient_no FROM patient WHERE id=?");
    sqlite3_bind_int(stmt, 1, patientId);
    std::string value;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        value = columnText(stmt, 0);
    }
    sqlite3_finalize(stmt);
    return value.empty() ? ("P" + stampTextMs()) : value;
}

/// 计算患者下一个订单序号。
int Database::nextOrderSequenceUnlocked(int patientId)
{
    sqlite3_stmt* stmt = prepare("SELECT COUNT(*) FROM orders WHERE patient_id=?");
    sqlite3_bind_int(stmt, 1, patientId);
    int count = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        count = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);
    return count + 1;
}

/// 基于毫秒时间戳生成不重复患者编号。
std::string Database::generatePatientNoUnlocked()
{
    for (int suffix = 0; suffix < 100; ++suffix) {
        std::string candidate = "P" + stampTextMs();
        if (suffix > 0) {
            candidate += toString(suffix);
        }
        sqlite3_stmt* stmt = prepare("SELECT COUNT(*) FROM patient WHERE patient_no=?");
        sqlite3_bind_text(stmt, 1, candidate.c_str(), -1, SQLITE_TRANSIENT);
        int count = 0;
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            count = sqlite3_column_int(stmt, 0);
        }
        sqlite3_finalize(stmt);
        if (count == 0) {
            return candidate;
        }
    }
    return "P" + stampTextMs();
}

/// 删除失去外键引用的扫描和订单记录。
void Database::cleanupOrphansUnlocked()
{
    execIgnoreError("DELETE FROM scan_result WHERE order_id NOT IN (SELECT id FROM orders);");
    execIgnoreError("DELETE FROM orders WHERE patient_id NOT IN (SELECT id FROM patient);");
}

/// 空数据库首次启动时写入一条演示患者。
void Database::seed()
{
    sqlite3_stmt* stmt = prepare("SELECT COUNT(*) FROM patient");
    int count = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        count = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);
    if (count > 0) {
        return;
    }
    Patient p;
    p.id = 0;
    p.patientNo = "P20260513001";
    p.name = "测试患者";
    p.gender = "女";
    p.age = 32;
    p.phone = "13800000000";
    p.doctor = "李医生";
    p.remark = "首诊";
    p.createdAt = nowText();
    createPatient(p);
}

} // namespace facescan
