export interface Patient {
  id: number
  patientNo: string
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
  previewPath: string
  createdAt: string
}

export interface PatientForm {
  patientNo: string
  name: string
  gender: string
  age: number | null
  phone: string
  doctor: string
  remark: string
}

