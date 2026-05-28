/** 患者基础资料及最近订单展示信息。 */
export interface Patient {
  /** 数据库主键。 */
  id: number
  /** 患者业务编号。 */
  patientNo: string
  /** 最近或当前订单编号。 */
  orderNo: string
  /** 患者姓名。 */
  name: string
  /** 患者性别。 */
  gender: string
  /** 患者年龄。 */
  age: number
  /** 联系手机号。 */
  phone: string
  /** 主治医生。 */
  doctor: string
  /** 拍摄或交付备注。 */
  remark: string
  /** 建档时间。 */
  createdAt: string
}

/** 单次采图或点云生成记录。 */
export interface ScanResult {
  /** 扫描记录主键。 */
  id: number
  /** 关联订单主键。 */
  orderId: number
  /** PLY 点云文件路径。 */
  plyPath: string
  /** 预览图路径。 */
  previewPath: string
  /** 原始采集图片路径集合。 */
  imagePaths: string[]
  /** 记录创建时间。 */
  createdAt: string
}

/** 点云重建接口返回结果。 */
export interface PointCloudResult {
  /** 关联订单主键。 */
  orderId: number
  /** 输出 PLY 文件路径。 */
  plyPath: string
  /** 点云 SVG 预览路径。 */
  previewPath: string
  /** 点云点数量。 */
  pointCount: number
  /** 点云三维包围盒。 */
  bounds: {
    /** 最小 X 坐标。 */
    minX: number
    /** 最小 Y 坐标。 */
    minY: number
    /** 最小 Z 坐标。 */
    minZ: number
    /** 最大 X 坐标。 */
    maxX: number
    /** 最大 Y 坐标。 */
    maxY: number
    /** 最大 Z 坐标。 */
    maxZ: number
  }
  /** 本次重建使用的源图像路径。 */
  sourceImages: string[]
}

/** 患者订单摘要。 */
export interface Order {
  /** 订单主键。 */
  id: number
  /** 所属患者主键。 */
  patientId: number
  /** 订单业务编号。 */
  orderNo: string
  /** 订单状态。 */
  status: string
  /** 订单创建时间。 */
  createdAt: string
  /** 扫描记录数量。 */
  scanCount: number
  /** 当前预览图路径。 */
  previewPath: string
  /** 最近生成的 PLY 路径。 */
  plyPath: string
}

/** 患者建档和编辑表单。 */
export interface PatientForm {
  /** 患者编号，只读展示。 */
  patientNo: string
  /** 订单编号，只读展示。 */
  orderNo: string
  /** 患者姓名。 */
  name: string
  /** 患者性别。 */
  gender: string
  /** 患者年龄，空值表示未填写。 */
  age: number | null
  /** 联系手机号。 */
  phone: string
  /** 主治医生。 */
  doctor: string
  /** 备注。 */
  remark: string
}

/** 系统设置响应。 */
export interface AppSettings {
  /** 患者数据保存根目录。 */
  dataRoot: string
  /** SQLite 数据库路径。 */
  databasePath: string
  /** 后端配置文件路径。 */
  configPath: string
  /** 当前相机模式。 */
  cameraMode: string
}
