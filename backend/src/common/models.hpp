#pragma once

#include <string>
#include <vector>

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
    std::string plyPath;
    std::string previewPath;
    std::vector<std::string> imagePaths;
    std::string createdAt;
};

struct Order {
    int id;
    int patientId;
    std::string orderNo;
    std::string status;
    std::string createdAt;
    int scanCount;
    std::string previewPath;
    std::string plyPath;
};

struct DataRootOrder {
    std::string orderNo;
    std::string createdAt;
    std::vector<std::string> imagePaths;
    std::string plyPath;
    std::string previewPath;
};

struct DataRootPatient {
    std::string patientNo;
    std::string name;
    std::string createdAt;
    std::vector<DataRootOrder> orders;
};

} // namespace facescan
