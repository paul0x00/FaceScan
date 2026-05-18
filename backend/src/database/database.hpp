#pragma once

#include "common/models.hpp"

#include <sqlite3.h>

#include <mutex>
#include <string>
#include <vector>

namespace facescan {

class Database {
public:
    explicit Database(const std::string& filePath);
    ~Database();

    std::vector<Patient> patients(const std::string& keyword, const std::string& date);
    Patient patientById(int id);
    int createPatient(const Patient& patient);
    bool deletePatient(int id);
    bool updatePatient(int id, const Patient& patient);
    int createOrder(int patientId);
    bool deleteOrder(int orderId);
    std::string orderNo(int orderId);
    std::string latestPreviewPathForOrder(int orderId);
    std::vector<Order> ordersForPatient(int patientId);
    int latestOrderId(int patientId);
    void addScanResult(int orderId, const std::string& previewPath);
    std::vector<ScanResult> scansForPatient(int patientId);

private:
    sqlite3* db_;
    std::mutex mutex_;

    void exec(const std::string& sql);
    void execIgnoreError(const std::string& sql);
    void addColumnIfMissing(const std::string& table, const std::string& column, const std::string& type);
    sqlite3_stmt* prepare(const std::string& sql);
    void stepDone(sqlite3_stmt* stmt);

    static std::string columnText(sqlite3_stmt* stmt, int column);
    static Patient readPatient(sqlite3_stmt* stmt);
    static void bindPatientFields(sqlite3_stmt* stmt, const Patient& patient);
    static std::string toString(int value);

    int createOrderUnlocked(int patientId);
    std::string patientNoUnlocked(int patientId);
    int nextOrderSequenceUnlocked(int patientId);
    std::string generatePatientNoUnlocked();
    void seed();
};

} // namespace facescan
