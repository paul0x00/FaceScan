#include "database/database.hpp"

#include "tests/test_utils.hpp"

#include <gtest/gtest.h>

using namespace facescan;

namespace {

/// 构造数据库测试使用的标准患者对象。
Patient makePatient()
{
    Patient patient;
    patient.id = 0;
    patient.patientNo = "";
    patient.orderNo = "";
    patient.name = "单元测试患者";
    patient.gender = "男";
    patient.age = 28;
    patient.phone = "13900000000";
    patient.doctor = "测试医生";
    patient.remark = "created by gtest";
    patient.createdAt = "2026-05-26 10:00:00";
    return patient;
}

} // namespace

/// 验证创建患者时会同步创建首个订单。
TEST(DatabaseTest, CreatesPatientWithInitialOrder)
{
    facescan_test::ScopedTempDir temp("database_create");
    Database db(temp.path() + "/facescan.sqlite3");

    const int patientId = db.createPatient(makePatient());
    ASSERT_GT(patientId, 0);

    const Patient loaded = db.patientById(patientId);
    EXPECT_EQ(patientId, loaded.id);
    EXPECT_EQ("单元测试患者", loaded.name);
    EXPECT_EQ("男", loaded.gender);
    EXPECT_EQ(28, loaded.age);
    EXPECT_FALSE(loaded.patientNo.empty());

    const int orderId = db.latestOrderId(patientId);
    EXPECT_GT(orderId, 0);
    EXPECT_FALSE(db.orderNo(orderId).empty());

    const std::vector<Order> orders = db.ordersForPatient(patientId);
    ASSERT_FALSE(orders.empty());
    EXPECT_EQ(orderId, orders.front().id);
}

/// 验证订单采集图片能正确写入扫描记录。
TEST(DatabaseTest, StoresCaptureImagesForOrder)
{
    facescan_test::ScopedTempDir temp("database_capture");
    Database db(temp.path() + "/facescan.sqlite3");

    const int patientId = db.createPatient(makePatient());
    const int orderId = db.latestOrderId(patientId);
    const std::vector<std::string> imagePaths = {
        temp.path() + "/left.svg",
        temp.path() + "/front.svg",
        temp.path() + "/right.svg",
        temp.path() + "/bottom.svg"
    };

    db.replaceOrderCapture(orderId, imagePaths);

    const std::vector<ScanResult> scans = db.scansForPatient(patientId);
    ASSERT_EQ(1u, scans.size());
    EXPECT_EQ(orderId, scans[0].orderId);
    EXPECT_EQ(imagePaths[0], scans[0].previewPath);
    EXPECT_EQ(imagePaths, scans[0].imagePaths);
}
