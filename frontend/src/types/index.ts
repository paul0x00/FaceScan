export interface Patient {
  id: number
  patientNo: string
  orderNo: string
  name: string
  gender: string
  age: number
  phone: string
  doctor: string
  remark: string
  createdAt: string
}

export interface ScanResult {
  id: number
  orderId: number
  plyPath: string
  previewPath: string
  imagePaths: string[]
  createdAt: string
}

export interface PointCloudResult {
  orderId: number
  plyPath: string
  previewPath: string
  pointCount: number
  bounds: {
    minX: number
    minY: number
    minZ: number
    maxX: number
    maxY: number
    maxZ: number
  }
  sourceImages: string[]
}

export interface Order {
  id: number
  patientId: number
  orderNo: string
  status: string
  createdAt: string
  scanCount: number
  previewPath: string
  plyPath: string
}

export interface PatientForm {
  patientNo: string
  orderNo: string
  name: string
  gender: string
  age: number | null
  phone: string
  doctor: string
  remark: string
}

export interface AppSettings {
  dataRoot: string
  databasePath: string
  configPath: string
  cameraMode: string
}
