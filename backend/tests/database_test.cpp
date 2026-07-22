#include "../src/FaceScanDatabase/database.hpp"

#include "test_utils.hpp"

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

/// 验证删除订单后新建订单不会复用旧编号。
TEST(DatabaseTest, CreateOrderDoesNotReuseDeletedOrderNumber)
{
    facescan_test::ScopedTempDir temp("database_order_sequence");
    Database db(temp.path() + "/facescan.sqlite3");

    const int patientId = db.createPatient(makePatient());
    const int firstOrderId = db.latestOrderId(patientId);
    const std::string firstOrderNo = db.orderNo(firstOrderId);
    ASSERT_FALSE(firstOrderNo.empty());
    EXPECT_NE(std::string::npos, firstOrderNo.find("-1"));

    const int secondOrderId = db.createOrder(patientId);
    const std::string secondOrderNo = db.orderNo(secondOrderId);
    EXPECT_NE(std::string::npos, secondOrderNo.find("-2"));

    EXPECT_TRUE(db.deleteOrder(secondOrderId));

    const int thirdOrderId = db.createOrder(patientId);
    const std::string thirdOrderNo = db.orderNo(thirdOrderId);
    EXPECT_NE(std::string::npos, thirdOrderNo.find("-3"));
}

/// 验证旧数据或异常序号状态下，新订单会跳过已存在的最大后缀。
TEST(DatabaseTest, CreateOrderUsesMaxExistingSuffixAsFallback)
{
    facescan_test::ScopedTempDir temp("database_order_sequence_fallback");
    Database db(temp.path() + "/facescan.sqlite3");

    const int patientId = db.createPatient(makePatient());
    const int firstOrderId = db.latestOrderId(patientId);
    ASSERT_TRUE(db.deleteOrder(firstOrderId));

    DataRootOrder fourthOrder;
    fourthOrder.orderNo = db.patientById(patientId).patientNo + "-4";
    fourthOrder.createdAt = "2026-06-09 10:00:00";

    DataRootPatient patient;
    patient.patientNo = db.patientById(patientId).patientNo;
    patient.name = makePatient().name;
    patient.createdAt = "2026-06-09 09:00:00";
    patient.orders.push_back(fourthOrder);

    db.syncDataRoot(std::vector<DataRootPatient>(1, patient));

    const int nextOrderId = db.createOrder(patientId);
    const std::string nextOrderNo = db.orderNo(nextOrderId);
    EXPECT_NE(std::string::npos, nextOrderNo.find("-5"));
}

/// 验证重复订单号会在数据库初始化时自动去重，并保留有扫描数据的记录。
TEST(DatabaseTest, DeduplicatesDuplicateOrdersOnStartup)
{
    facescan_test::ScopedTempDir temp("database_order_dedup");
    const std::string dbPath = temp.path() + "/facescan.sqlite3";
    sqlite3* raw = NULL;
    ASSERT_EQ(SQLITE_OK, sqlite3_open(dbPath.c_str(), &raw));
    ASSERT_EQ(SQLITE_OK, sqlite3_exec(raw,
        "CREATE TABLE patient ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "patient_no TEXT NOT NULL,"
        "name TEXT NOT NULL,"
        "gender TEXT,"
        "age INTEGER DEFAULT 0,"
        "phone TEXT,"
        "doctor TEXT,"
        "remark TEXT,"
        "created_at TEXT NOT NULL);",
        NULL, NULL, NULL));
    ASSERT_EQ(SQLITE_OK, sqlite3_exec(raw,
        "CREATE TABLE orders ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "patient_id INTEGER NOT NULL,"
        "order_no TEXT,"
        "status TEXT NOT NULL,"
        "created_at TEXT NOT NULL);",
        NULL, NULL, NULL));
    ASSERT_EQ(SQLITE_OK, sqlite3_exec(raw,
        "CREATE TABLE scan_result ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "order_id INTEGER NOT NULL,"
        "ply_path TEXT,"
        "image_paths TEXT,"
        "preview_path TEXT NOT NULL,"
        "created_at TEXT NOT NULL);",
        NULL, NULL, NULL));
    ASSERT_EQ(SQLITE_OK, sqlite3_exec(raw,
        "INSERT INTO patient(id,patient_no,name,created_at) VALUES(1,'P20260605081443573','张三','2026-06-05 08:14:43');",
        NULL, NULL, NULL));
    ASSERT_EQ(SQLITE_OK, sqlite3_exec(raw,
        "INSERT INTO orders(id,patient_id,order_no,status,created_at) VALUES"
        "(24,1,'P20260605081443573-4','synced','2026-06-05 13:52:03'),"
        "(82,1,'P20260605081443573-4','created','2026-06-09 17:48:25'),"
        "(84,1,'P20260605081443573-4','created','2026-06-09 17:52:13');",
        NULL, NULL, NULL));
    ASSERT_EQ(SQLITE_OK, sqlite3_exec(raw,
        "INSERT INTO scan_result(order_id,ply_path,image_paths,preview_path,created_at) VALUES"
        "(24,'/tmp/existing.ply','/tmp/front.ppm','/tmp/preview.svg','2026-06-05 15:03:10');",
        NULL, NULL, NULL));
    sqlite3_close(raw);

    Database reopened(dbPath);
    const std::vector<Order> orders = reopened.ordersForPatient(1);
    ASSERT_EQ(1u, orders.size());
    EXPECT_EQ("P20260605081443573-4", orders[0].orderNo);
    EXPECT_EQ(1, orders[0].scanCount);

    sqlite3* verify = NULL;
    ASSERT_EQ(SQLITE_OK, sqlite3_open(dbPath.c_str(), &verify));
    ASSERT_EQ(SQLITE_CONSTRAINT, sqlite3_exec(verify,
        "INSERT INTO orders(patient_id,order_no,status,created_at) VALUES"
        "(1,'P20260605081443573-4','created','2026-06-09 18:00:00');",
        NULL, NULL, NULL));
    sqlite3_close(verify);
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
