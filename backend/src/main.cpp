#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/asio.hpp>
#include <sqlite3.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <cerrno>
#include <vector>

#if defined(_WIN32)
#include <direct.h>
#else
#include <sys/stat.h>
#include <sys/types.h>
#endif

namespace beast = boost::beast;
namespace http = beast::http;
namespace asio = boost::asio;
using tcp = asio::ip::tcp;

namespace {

struct Patient {
    int id;
    std::string patientNo;
    std::string orderNo;
    std::string name;
    std::string gender;
    int age;
    std::string phone;
    std::string doctor;
    std::string remark;
    std::string createdAt;
};

struct ScanResult {
    int id;
    int orderId;
    std::string previewPath;
    std::string createdAt;
};

struct Order {
    int id;
    int patientId;
    std::string orderNo;
    std::string status;
    std::string createdAt;
    int scanCount;
};

std::string nowText()
{
    std::time_t t = std::time(NULL);
    std::tm tmValue;
#if defined(_WIN32)
    localtime_s(&tmValue, &t);
#else
    localtime_r(&t, &tmValue);
#endif
    std::ostringstream os;
    os << std::put_time(&tmValue, "%Y-%m-%d %H:%M:%S");
    return os.str();
}

std::string stampText()
{
    std::time_t t = std::time(NULL);
    std::tm tmValue;
#if defined(_WIN32)
    localtime_s(&tmValue, &t);
#else
    localtime_r(&t, &tmValue);
#endif
    std::ostringstream os;
    os << std::put_time(&tmValue, "%Y%m%d_%H%M%S");
    return os.str();
}

std::string stampTextMs()
{
    const std::chrono::system_clock::time_point now = std::chrono::system_clock::now();
    const std::time_t t = std::chrono::system_clock::to_time_t(now);
    const long ms = static_cast<long>(std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000);
    std::tm tmValue;
#if defined(_WIN32)
    localtime_s(&tmValue, &t);
#else
    localtime_r(&t, &tmValue);
#endif
    std::ostringstream os;
    os << std::put_time(&tmValue, "%Y%m%d%H%M%S") << std::setw(3) << std::setfill('0') << ms;
    return os.str();
}

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

bool makeDirectory(const std::string& path)
{
    if (path.empty()) {
        return true;
    }
#if defined(_WIN32)
    if (_mkdir(path.c_str()) == 0 || errno == EEXIST) {
        return true;
    }
#else
    if (mkdir(path.c_str(), 0755) == 0 || errno == EEXIST) {
        return true;
    }
#endif
    return false;
}

void ensureDirectory(const std::string& path)
{
    std::string current;
    for (std::size_t i = 0; i < path.size(); ++i) {
        current.push_back(path[i]);
        if (path[i] == '/' || i + 1 == path.size()) {
            if (current.empty() || current == "/") {
                continue;
            }
            makeDirectory(current);
        }
    }
}

std::string parentDirectory(const std::string& path)
{
    const std::size_t pos = path.find_last_of("/\\");
    if (pos == std::string::npos) {
        return "";
    }
    return path.substr(0, pos);
}

std::string shellQuote(const std::string& path)
{
#if defined(_WIN32)
    std::string out = "\"";
    for (std::string::const_iterator it = path.begin(); it != path.end(); ++it) {
        if (*it == '"') {
            out += "\\\"";
        } else {
            out.push_back(*it);
        }
    }
    out += "\"";
    return out;
#else
    std::string out = "'";
    for (std::string::const_iterator it = path.begin(); it != path.end(); ++it) {
        if (*it == '\'') {
            out += "'\\''";
        } else {
            out.push_back(*it);
        }
    }
    out += "'";
    return out;
#endif
}

bool openDirectory(const std::string& path)
{
    if (path.empty()) {
        return false;
    }
#if defined(_WIN32)
    const std::string command = "start \"\" " + shellQuote(path);
#elif defined(__APPLE__)
    const std::string command = "open " + shellQuote(path);
#else
    const std::string command = "xdg-open " + shellQuote(path);
#endif
    return std::system(command.c_str()) == 0;
}

std::string readFileText(const std::string& path)
{
    std::ifstream file(path.c_str(), std::ios::binary);
    if (!file) {
        return "";
    }
    std::ostringstream os;
    os << file.rdbuf();
    return os.str();
}

struct AppConfig {
    AppConfig()
        : backendPort(8080),
          databasePath("data/db/facescan.sqlite3"),
          imageRoot("data/images"),
          cameraMode("mock")
    {
    }

    unsigned short backendPort;
    std::string databasePath;
    std::string imageRoot;
    std::string cameraMode;
};

AppConfig loadAppConfig()
{
    AppConfig config;
    std::string body = readFileText("config/app.json");
    if (body.empty()) {
        body = readFileText("config/app.example.json");
    }
    if (body.empty()) {
        return config;
    }

    const int port = jsonIntValue(body, "backendPort");
    const std::string database = jsonStringValue(body, "database");
    const std::string imageRoot = jsonStringValue(body, "imageRoot");
    const std::string cameraMode = jsonStringValue(body, "cameraMode");
    if (port > 0 && port <= 65535) {
        config.backendPort = static_cast<unsigned short>(port);
    }
    if (!database.empty()) {
        config.databasePath = database;
    }
    if (!imageRoot.empty()) {
        config.imageRoot = imageRoot;
    }
    if (!cameraMode.empty()) {
        config.cameraMode = cameraMode;
    }
    return config;
}

class Database {
public:
    explicit Database(const std::string& filePath) : db_(NULL)
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

    ~Database()
    {
        if (db_) {
            sqlite3_close(db_);
        }
    }

    std::vector<Patient> patients(const std::string& keyword, const std::string& date)
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

    Patient patientById(int id)
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

    int createPatient(const Patient& patient)
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

    bool deletePatient(int id)
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

    bool updatePatient(int id, const Patient& patient)
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

    int createOrder(int patientId)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return createOrderUnlocked(patientId);
    }

    bool deleteOrder(int orderId)
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

    std::string orderNo(int orderId)
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

    std::string latestPreviewPathForOrder(int orderId)
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

    std::vector<Order> ordersForPatient(int patientId)
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

    int latestOrderId(int patientId)
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

    void addScanResult(int orderId, const std::string& previewPath)
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

    std::vector<ScanResult> scansForPatient(int patientId)
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

private:
    sqlite3* db_;
    std::mutex mutex_;

    void exec(const std::string& sql)
    {
        char* err = NULL;
        if (sqlite3_exec(db_, sql.c_str(), NULL, NULL, &err) != SQLITE_OK) {
            const std::string message = err ? err : "SQLite error";
            sqlite3_free(err);
            throw std::runtime_error(message);
        }
    }

    void execIgnoreError(const std::string& sql)
    {
        char* err = NULL;
        if (sqlite3_exec(db_, sql.c_str(), NULL, NULL, &err) != SQLITE_OK) {
            sqlite3_free(err);
        }
    }

    void addColumnIfMissing(const std::string& table, const std::string& column, const std::string& type)
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

    sqlite3_stmt* prepare(const std::string& sql)
    {
        sqlite3_stmt* stmt = NULL;
        if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, NULL) != SQLITE_OK) {
            throw std::runtime_error(sqlite3_errmsg(db_));
        }
        return stmt;
    }

    void stepDone(sqlite3_stmt* stmt)
    {
        if (sqlite3_step(stmt) != SQLITE_DONE) {
            throw std::runtime_error(sqlite3_errmsg(db_));
        }
    }

    static std::string columnText(sqlite3_stmt* stmt, int column)
    {
        const unsigned char* text = sqlite3_column_text(stmt, column);
        return text ? reinterpret_cast<const char*>(text) : "";
    }

    static Patient readPatient(sqlite3_stmt* stmt)
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

    static void bindPatientFields(sqlite3_stmt* stmt, const Patient& patient)
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

    int createOrderUnlocked(int patientId)
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

    static std::string toString(int value)
    {
        std::ostringstream os;
        os << value;
        return os.str();
    }

    std::string patientNoUnlocked(int patientId)
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

    int nextOrderSequenceUnlocked(int patientId)
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

    std::string generatePatientNoUnlocked()
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

    void seed()
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
};

class CameraManager {
public:
    explicit CameraManager(const std::string& imageRoot)
        : imageRoot_(normalizeRoot(imageRoot)), streaming_(false), frameIndex_(0)
    {
    }

    void start() { streaming_ = true; }
    void stop() { streaming_ = false; }
    bool streaming() const { return streaming_; }

    static std::string normalizeRoot(const std::string& path)
    {
        if (path.empty()) {
            return "data/images";
        }
        if (path[path.size() - 1] == '/' || path[path.size() - 1] == '\\') {
            return path.substr(0, path.size() - 1);
        }
        return path;
    }

    std::string frameSvg(const std::string& view)
    {
        const int idx = ++frameIndex_;
        const std::string label = cameraLabel(view);
        const int hue = colorHue(view);
        std::ostringstream os;
        os << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"640\" height=\"400\" viewBox=\"0 0 640 400\">";
        os << "<defs><linearGradient id=\"g\" x1=\"0\" x2=\"1\"><stop offset=\"0\" stop-color=\"hsl(" << hue << ",62%,72%)\"/><stop offset=\"1\" stop-color=\"hsl(" << (hue + 42) % 360 << ",55%,58%)\"/></linearGradient></defs>";
        os << "<rect width=\"640\" height=\"400\" fill=\"url(#g)\"/>";
        os << "<g opacity=\"0.34\" stroke=\"#ffffff\" stroke-width=\"2\">";
        for (int i = 0; i < 12; ++i) {
            const int x = (idx * 7 + i * 61) % 700 - 30;
            os << "<circle cx=\"" << x << "\" cy=\"" << (55 + i * 28) << "\" r=\"" << (18 + (idx + i) % 26) << "\" fill=\"none\"/>";
        }
        os << "</g>";
        os << "<rect x=\"24\" y=\"24\" width=\"592\" height=\"352\" rx=\"10\" fill=\"none\" stroke=\"rgba(255,255,255,.68)\" stroke-width=\"3\"/>";
        os << "<text x=\"320\" y=\"192\" text-anchor=\"middle\" font-family=\"Arial, sans-serif\" font-size=\"28\" font-weight=\"700\" fill=\"#1f2933\">" << label << "</text>";
        os << "<text x=\"320\" y=\"230\" text-anchor=\"middle\" font-family=\"Arial, sans-serif\" font-size=\"16\" fill=\"#2f3d4a\">" << (streaming_ ? "实时预览" : "预览暂停") << " · Frame " << idx << "</text>";
        os << "</svg>";
        return os.str();
    }

    std::vector<std::string> capture(int orderId)
    {
        ensureDirectory(imageRoot_);
        const std::string stamp = stampTextMs();
        const char* views[] = { "left", "front", "right", "bottom" };
        std::vector<std::string> paths;
        for (int i = 0; i < 4; ++i) {
            const std::string filePath = imageRoot_ + "/order_" + toString(orderId) + "_" + stamp + "_" + views[i] + ".svg";
            std::ofstream file(filePath.c_str(), std::ios::binary);
            file << frameSvg(views[i]);
            file.close();
            paths.push_back(filePath);
        }
        return paths;
    }

private:
    std::string imageRoot_;
    std::atomic<bool> streaming_;
    std::atomic<int> frameIndex_;

    static std::string toString(int value)
    {
        std::ostringstream os;
        os << value;
        return os.str();
    }

    static std::string cameraLabel(const std::string& view)
    {
        if (view == "left") return "左侧相机图像";
        if (view == "front") return "正面相机图像";
        if (view == "right") return "右侧相机图像";
        if (view == "bottom") return "下方相机图像";
        return "相机图像";
    }

    static int colorHue(const std::string& view)
    {
        if (view == "left") return 174;
        if (view == "front") return 206;
        if (view == "right") return 28;
        if (view == "bottom") return 118;
        return 190;
    }
};

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
       << jsonPair("scanCount", order.scanCount)
       << "}";
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

class App {
public:
    explicit App(const AppConfig& config)
        : database_(config.databasePath),
          camera_(config.imageRoot),
          imageRoot_(CameraManager::normalizeRoot(config.imageRoot))
    {
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
            if (req.method() == http::verb::get && path == "/api/patients") {
                return listPatients(req, target);
            }
            if (req.method() == http::verb::post && path == "/api/patients") {
                Patient p = patientFromBody(req.body());
                const int id = database_.createPatient(p);
                Patient created = database_.patientById(id);
                const int orderId = database_.latestOrderId(id);
                return jsonResponse(req, "{\"id\":" + toString(id)
                    + ",\"orderId\":" + toString(orderId)
                    + ",\"patientNo\":\"" + escapeJson(created.patientNo)
                    + "\",\"orderNo\":\"" + escapeJson(created.orderNo) + "\"}");
            }
            if (req.method() == http::verb::put && path.find("/api/patients/") == 0) {
                const int id = std::atoi(path.substr(std::string("/api/patients/").size()).c_str());
                const bool ok = database_.updatePatient(id, patientFromBody(req.body()));
                return jsonResponse(req, std::string("{\"ok\":") + (ok ? "true" : "false") + "}");
            }
            if (req.method() == http::verb::delete_ && path.find("/api/patients/") == 0) {
                const int id = std::atoi(path.substr(std::string("/api/patients/").size()).c_str());
                const bool ok = database_.deletePatient(id);
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
                return jsonResponse(req, "{\"id\":" + toString(orderId) + ",\"orderNo\":\"" + escapeJson(database_.orderNo(orderId)) + "\"}");
            }
            if (req.method() == http::verb::delete_ && path.find("/api/orders/") == 0) {
                const int id = std::atoi(path.substr(std::string("/api/orders/").size()).c_str());
                const bool ok = database_.deleteOrder(id);
                return jsonResponse(req, std::string("{\"ok\":") + (ok ? "true" : "false") + "}");
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
                return svgResponse(req, camera_.frameSvg(view));
            }
            if (req.method() == http::verb::post && path == "/api/camera/capture") {
                return capture(req);
            }
            if (req.method() == http::verb::post && path == "/api/export/package") {
                return jsonResponse(req, "{\"ok\":true,\"message\":\"数据已完成本地压缩打包（MVP占位）\"}");
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

    static std::string toString(int value)
    {
        std::ostringstream os;
        os << value;
        return os.str();
    }

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

    http::response<http::string_body> listPatients(const http::request<http::string_body>& req, const std::string& target)
    {
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
               << jsonPair("previewPath", scans[i].previewPath) << ","
               << jsonPair("createdAt", scans[i].createdAt)
               << "}";
        }
        os << "]}";
        return jsonResponse(req, os.str());
    }

    http::response<http::string_body> listOrders(const http::request<http::string_body>& req, int patientId)
    {
        const std::vector<Order> orders = database_.ordersForPatient(patientId);
        std::ostringstream os;
        os << "{\"items\":[";
        for (std::size_t i = 0; i < orders.size(); ++i) {
            if (i) os << ",";
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
        const std::vector<std::string> paths = camera_.capture(orderId);
        for (std::size_t i = 0; i < paths.size(); ++i) {
            database_.addScanResult(orderId, paths[i]);
        }
        std::ostringstream os;
        os << "{\"orderId\":" << orderId << ",\"paths\":[";
        for (std::size_t i = 0; i < paths.size(); ++i) {
            if (i) os << ",";
            os << "\"" << escapeJson(paths[i]) << "\"";
        }
        os << "]}";
        return jsonResponse(req, os.str());
    }
};

void doSession(tcp::socket socket, std::shared_ptr<App> app)
{
    beast::flat_buffer buffer;
    try {
        for (;;) {
            http::request<http::string_body> req;
            http::read(socket, buffer, req);
            http::response<http::string_body> res = app->handle(req);
            const bool close = res.need_eof();
            http::write(socket, res);
            if (close) {
                break;
            }
        }
    } catch (const std::exception&) {
    }
    beast::error_code ec;
    socket.shutdown(tcp::socket::shutdown_send, ec);
}

} // namespace

int main(int argc, char* argv[])
{
    try {
        const AppConfig config = loadAppConfig();
        const unsigned short port = argc > 1 ? static_cast<unsigned short>(std::atoi(argv[1])) : config.backendPort;
        asio::io_context ioc(1);
        tcp::acceptor acceptor(ioc, tcp::endpoint(tcp::v4(), port));
        std::shared_ptr<App> app(new App(config));
        std::cout << "FaceScan backend listening on http://127.0.0.1:" << port << std::endl;
        for (;;) {
            tcp::socket socket(ioc);
            acceptor.accept(socket);
            std::thread(&doSession, std::move(socket), app).detach();
        }
    } catch (const std::exception& e) {
        std::cerr << "Backend failed: " << e.what() << std::endl;
        return 1;
    }
}
