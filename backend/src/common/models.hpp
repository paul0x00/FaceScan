#pragma once

#include <string>
#include <vector>

namespace facescan {

/// 患者基础资料及其当前关联订单的展示字段。
struct Patient {
    /// 数据库主键；未持久化对象为 0。
    int id;
    /// 患者业务编号，创建患者时自动生成。
    std::string patientNo;
    /// 当前或最近订单编号，用于前端只读展示。
    std::string orderNo;
    /// 最新订单正面拍摄图路径，用于患者列表缩略图。
    std::string thumbnailPath;
    /// 患者姓名。
    std::string name;
    /// 患者性别。
    std::string gender;
    /// 患者年龄。
    int age;
    /// 联系手机号。
    std::string phone;
    /// 主治医生。
    std::string doctor;
    /// 拍摄或交付备注。
    std::string remark;
    /// 建档时间，格式为 yyyy-MM-dd HH:mm:ss。
    std::string createdAt;
};

/// 单次扫描或点云生成结果。
struct ScanResult {
    /// 扫描记录主键。
    int id;
    /// 关联订单主键。
    int orderId;
    /// PLY 点云文件路径；仅采图记录可为空。
    std::string plyPath;
    /// 前端预览图路径。
    std::string previewPath;
    /// 本次采集写入的原始图像路径集合。
    std::vector<std::string> imagePaths;
    /// 记录创建时间。
    std::string createdAt;
};

/// 患者订单摘要。
struct Order {
    /// 订单主键。
    int id;
    /// 所属患者主键。
    int patientId;
    /// 订单业务编号。
    std::string orderNo;
    /// 订单状态，如 created、captured、synced。
    std::string status;
    /// 订单创建时间。
    std::string createdAt;
    /// 订单最后修改时间；有扫描记录时取最新扫描时间，否则取创建时间。
    std::string updatedAt;
    /// 订单下扫描记录数量。
    int scanCount;
    /// 当前优先展示的预览图路径。
    std::string previewPath;
    /// 最近生成的 PLY 点云路径。
    std::string plyPath;
};

/// 从数据目录反扫得到的订单文件索引。
struct DataRootOrder {
    /// 订单目录名或订单编号。
    std::string orderNo;
    /// 从目录名或文件修改时间推导出的创建时间。
    std::string createdAt;
    /// 订单目录内相关文件的最后修改时间。
    std::string updatedAt;
    /// 订单目录内可识别的图像文件。
    std::vector<std::string> imagePaths;
    /// 订单目录内的 PLY 点云文件。
    std::string plyPath;
    /// 订单目录内的预览图文件。
    std::string previewPath;
};

/// 从数据目录反扫得到的患者文件索引。
struct DataRootPatient {
    /// 患者目录中的患者编号。
    std::string patientNo;
    /// 患者目录中的患者姓名。
    std::string name;
    /// 从患者编号或目录修改时间推导出的创建时间。
    std::string createdAt;
    /// 患者目录下的订单集合。
    std::vector<DataRootOrder> orders;
};

} // namespace facescan
