#pragma once

#include <string>

namespace facescan {

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

} // namespace facescan
