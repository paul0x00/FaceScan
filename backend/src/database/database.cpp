#include "database/database.hpp"

#include "common/file_utils.hpp"
#include "common/time_utils.hpp"

#include <sstream>
#include <stdexcept>

namespace facescan {

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
         "preview_path TEXT NOT NULL,"
         "created_at TEXT NOT NULL,"
         "FOREIGN KEY(order_id) REFERENCES orders(id)"
         ");");
    seed();
}

Database::~Database()
{
    if (db_) {
        sqlite3_close(db_);
    }
}

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

bool Database::deletePatient(int id)
{
    std::lock_guard<std::mutex> lock(mutex_);
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
    return changed;
}

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

int Database::createOrder(int patientId)
{
    std::lock_guard<std::mutex> lock(mutex_);
    return createOrderUnlocked(patientId);
}

bool Database::deleteOrder(int orderId)
{
    std::lock_guard<std::mutex> lock(mutex_);
    sqlite3_stmt* scanStmt = prepare("DELETE FROM scan_result WHERE order_id=?");
    sqlite3_bind_int(scanStmt, 1, orderId);
    stepDone(scanStmt);
    sqlite3_finalize(scanStmt);

    sqlite3_stmt* stmt = prepare("DELETE FROM orders WHERE id=?");
    sqlite3_bind_int(stmt, 1, orderId);
    stepDone(stmt);
    const bool changed = sqlite3_changes(db_) > 0;
    sqlite3_finalize(stmt);
    return changed;
}

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

std::string Database::latestPreviewPathForOrder(int orderId)
{
    std::lock_guard<std::mutex> lock(mutex_);
    sqlite3_stmt* stmt = prepare("SELECT preview_path FROM scan_result WHERE order_id=? ORDER BY id DESC LIMIT 1");
    sqlite3_bind_int(stmt, 1, orderId);
    std::string value;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        value = columnText(stmt, 0);
    }
    sqlite3_finalize(stmt);
    return value;
}

std::vector<Order> Database::ordersForPatient(int patientId)
{
    std::lock_guard<std::mutex> lock(mutex_);
    sqlite3_stmt* stmt = prepare(
        "SELECT orders.id, orders.patient_id, orders.order_no, orders.status, orders.created_at, "
        "COUNT(scan_result.id) AS scan_count "
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
        orders.push_back(order);
    }
    sqlite3_finalize(stmt);
    return orders;
}

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

void Database::addScanResult(int orderId, const std::string& previewPath)
{
    std::lock_guard<std::mutex> lock(mutex_);
    sqlite3_stmt* stmt = prepare("INSERT INTO scan_result(order_id, ply_path, preview_path, created_at) VALUES(?,?,?,?)");
    const std::string empty;
    const std::string created = nowText();
    sqlite3_bind_int(stmt, 1, orderId);
    sqlite3_bind_text(stmt, 2, empty.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, previewPath.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, created.c_str(), -1, SQLITE_TRANSIENT);
    stepDone(stmt);
    sqlite3_finalize(stmt);
}

std::vector<ScanResult> Database::scansForPatient(int patientId)
{
    std::lock_guard<std::mutex> lock(mutex_);
    sqlite3_stmt* stmt = prepare(
        "SELECT scan_result.id, scan_result.order_id, scan_result.preview_path, scan_result.created_at "
        "FROM scan_result JOIN orders ON orders.id=scan_result.order_id "
        "WHERE orders.patient_id=? ORDER BY scan_result.id DESC");
    sqlite3_bind_int(stmt, 1, patientId);
    std::vector<ScanResult> scans;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        ScanResult s;
        s.id = sqlite3_column_int(stmt, 0);
        s.orderId = sqlite3_column_int(stmt, 1);
        s.previewPath = columnText(stmt, 2);
        s.createdAt = columnText(stmt, 3);
        scans.push_back(s);
    }
    sqlite3_finalize(stmt);
    return scans;
}

void Database::exec(const std::string& sql)
{
    char* err = NULL;
    if (sqlite3_exec(db_, sql.c_str(), NULL, NULL, &err) != SQLITE_OK) {
        const std::string message = err ? err : "SQLite error";
        sqlite3_free(err);
        throw std::runtime_error(message);
    }
}

void Database::execIgnoreError(const std::string& sql)
{
    char* err = NULL;
    if (sqlite3_exec(db_, sql.c_str(), NULL, NULL, &err) != SQLITE_OK) {
        sqlite3_free(err);
    }
}

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

sqlite3_stmt* Database::prepare(const std::string& sql)
{
    sqlite3_stmt* stmt = NULL;
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, NULL) != SQLITE_OK) {
        throw std::runtime_error(sqlite3_errmsg(db_));
    }
    return stmt;
}

void Database::stepDone(sqlite3_stmt* stmt)
{
    if (sqlite3_step(stmt) != SQLITE_DONE) {
        throw std::runtime_error(sqlite3_errmsg(db_));
    }
}

std::string Database::columnText(sqlite3_stmt* stmt, int column)
{
    const unsigned char* text = sqlite3_column_text(stmt, column);
    return text ? reinterpret_cast<const char*>(text) : "";
}

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

std::string Database::toString(int value)
{
    std::ostringstream os;
    os << value;
    return os.str();
}

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
