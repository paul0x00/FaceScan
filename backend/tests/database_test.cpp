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
    EXPECT_EQ(orders.front().createdAt, orders.front().updatedAt);
}

/// 验证订单采集图片能正确写入扫描记录。
TEST(DatabaseTest, StoresCaptureImagesForOrder)
{
    facescan_test::ScopedTempDir temp("database_capture");
    Database db(temp.path() + "/facescan.sqlite3");

    const int patientId = db.createPatient(makePatient());
    const int orderId = db.latestOrderId(patientId);
    const std::string orderNo = db.orderNo(orderId);
    const std::vector<std::string> imagePaths = {
        temp.path() + "/" + orderNo + "_left.svg",
        temp.path() + "/" + orderNo + "_front.svg",
        temp.path() + "/" + orderNo + "_right.svg",
        temp.path() + "/" + orderNo + "_bottom.svg"
    };

    db.replaceOrderCapture(orderId, imagePaths);

    const std::vector<ScanResult> scans = db.scansForPatient(patientId);
    ASSERT_EQ(1u, scans.size());
    EXPECT_EQ(orderId, scans[0].orderId);
    EXPECT_EQ(imagePaths[0], scans[0].previewPath);
    EXPECT_EQ(imagePaths, scans[0].imagePaths);

    const std::vector<Order> orders = db.ordersForPatient(patientId);
    ASSERT_FALSE(orders.empty());
    EXPECT_EQ(scans[0].createdAt, orders.front().updatedAt);

    const std::vector<Patient> patients = db.patients("", "");
    ASSERT_FALSE(patients.empty());
    EXPECT_EQ(imagePaths[1], patients.front().thumbnailPath);
}

/// 验证患者缩略图只来自最新订单。
TEST(DatabaseTest, PatientThumbnailUsesLatestOrderOnly)
{
    facescan_test::ScopedTempDir temp("database_latest_thumbnail");
    Database db(temp.path() + "/facescan.sqlite3");

    const int patientId = db.createPatient(makePatient());
    const int firstOrderId = db.latestOrderId(patientId);
    const std::string firstOrderNo = db.orderNo(firstOrderId);
    const std::vector<std::string> imagePaths = {
        temp.path() + "/" + firstOrderNo + "_left.svg",
        temp.path() + "/" + firstOrderNo + "_front.svg",
        temp.path() + "/" + firstOrderNo + "_right.svg",
        temp.path() + "/" + firstOrderNo + "_bottom.svg"
    };
    db.replaceOrderCapture(firstOrderId, imagePaths);

    const int latestOrderId = db.createOrder(patientId);
    ASSERT_GT(latestOrderId, firstOrderId);

    const std::vector<Patient> patients = db.patients("", "");
    ASSERT_FALSE(patients.empty());
    EXPECT_EQ("", patients.front().thumbnailPath);
}

/// 验证数据目录同步会保留订单文件的最后修改时间。
TEST(DatabaseTest, SyncDataRootUsesOrderUpdatedAt)
{
    facescan_test::ScopedTempDir temp("database_sync_updated");
    Database db(temp.path() + "/facescan.sqlite3");

    DataRootOrder order;
    order.orderNo = "P20260605081443573-1";
    order.createdAt = "2026-06-05 08:14:43";
    order.updatedAt = "2026-06-05 15:20:31";
    order.imagePaths.push_back(temp.path() + "/front.svg");
    order.previewPath = order.imagePaths.front();

    DataRootPatient patient;
    patient.patientNo = "P20260605081443573";
    patient.name = "张三";
    patient.createdAt = "2026-06-05 08:14:43";
    patient.orders.push_back(order);

    db.syncDataRoot(std::vector<DataRootPatient>(1, patient));

    const std::vector<Patient> patients = db.patients("", "");
    ASSERT_FALSE(patients.empty());
    const std::vector<Order> orders = db.ordersForPatient(patients.front().id);
    ASSERT_FALSE(orders.empty());
    EXPECT_EQ(order.createdAt, orders.front().createdAt);
    EXPECT_EQ(order.updatedAt, orders.front().updatedAt);
}
