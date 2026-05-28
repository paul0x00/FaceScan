import axios from 'axios'
import type { AppSettings, Order, Patient, PatientForm, PointCloudResult, ScanResult } from '../types'

/** 统一 Axios 实例，开发环境通过 Vite 代理访问后端 /api。 */
const api = axios.create({
  baseURL: '/api',
  timeout: 60000
})

/** 为所有请求自动附加本地登录 token。 */
api.interceptors.request.use((config) => {
  const token = localStorage.getItem('facescan.token')
  if (token) {
    config.headers.Authorization = `Bearer ${token}`
  }
  return config
})

/** 登录本地工作站并持久化开发 token。 */
export async function login(username: string, password: string) {
  const { data } = await api.post('/login', { username, password })
  localStorage.setItem('facescan.token', data.token)
  return data
}

/** 查询患者列表。 */
export async function fetchPatients(params: { keyword?: string; date?: string }) {
  const { data } = await api.get<{ items: Patient[] }>('/patients', { params })
  return data.items
}

/** 创建患者并由后端自动生成首个订单。 */
export async function createPatient(form: PatientForm) {
  const { data } = await api.post<{ id: number; orderId: number; patientNo: string; orderNo: string }>('/patients', form)
  return data
}

/** 更新患者基础资料。 */
export async function updatePatient(id: number, form: PatientForm) {
  const { data } = await api.put<{ ok: boolean }>(`/patients/${id}`, form)
  return data.ok
}

/** 删除患者及其订单数据。 */
export async function deletePatient(id: number) {
  const { data } = await api.delete<{ ok: boolean }>(`/patients/${id}`)
  return data.ok
}

/** 为患者创建新订单。 */
export async function createOrder(patientId: number) {
  const { data } = await api.post<{ id: number; orderNo: string }>('/orders', { patientId })
  return data
}

/** 查询患者的订单列表。 */
export async function fetchOrders(patientId: number) {
  const { data } = await api.get<{ items: Order[] }>(`/patients/${patientId}/orders`)
  return data.items
}

/** 删除订单及其采集记录。 */
export async function deleteOrder(id: number) {
  const { data } = await api.delete<{ ok: boolean }>(`/orders/${id}`)
  return data.ok
}

/** 请求后端打开订单文件夹。 */
export async function openOrderFolder(id: number) {
  const { data } = await api.post<{ ok: boolean; path: string }>(`/orders/${id}/open-folder`)
  return data
}

/** 触发订单点云重建。 */
export async function reconstructOrder(id: number, options: { columns?: number; rows?: number } = {}) {
  const { data } = await api.post<PointCloudResult>(`/orders/${id}/reconstruct`, {
    columns: options.columns ?? 96,
    rows: options.rows ?? 72
  })
  return data
}

/** 构造 PLY 文件读取 URL。 */
export function pointCloudFileUrl(path: string) {
  return `/api/pointcloud/file?path=${encodeURIComponent(path)}`
}

/** 构造图片文件读取 URL。 */
export function imageFileUrl(path: string) {
  return `/api/files/image?path=${encodeURIComponent(path)}`
}

/** 开启相机预览。 */
export async function startCamera() {
  await api.post('/camera/start')
}

/** 停止相机预览。 */
export async function stopCamera() {
  await api.post('/camera/stop')
}

/** 同步采图并返回写入路径。 */
export async function capture(patientId: number, orderId?: number) {
  const { data } = await api.post<{ orderId: number; paths: string[] }>('/camera/capture', { patientId, orderId })
  return data
}

/** 查询患者扫描记录。 */
export async function fetchScans(patientId: number) {
  const { data } = await api.get<{ items: ScanResult[] }>(`/patients/${patientId}/scans`)
  return data.items
}

/** 打包患者指定订单的本地交付数据。 */
export async function packageData(patientId: number, orderId?: number) {
  const { data } = await api.post<{ ok: boolean; message: string; path: string }>('/export/package', { patientId, orderId })
  return data
}

/** 调用预留的云端上传接口。 */
export async function uploadData(patientId: number) {
  const { data } = await api.post<{ ok: boolean; message: string }>('/export/upload', { patientId })
  return data
}

/** 获取系统设置。 */
export async function fetchSettings() {
  const { data } = await api.get<AppSettings>('/settings')
  return data
}

/** 保存可编辑系统设置。 */
export async function saveSettings(settings: Pick<AppSettings, 'dataRoot'>) {
  const { data } = await api.put<AppSettings>('/settings', settings)
  return data
}

/** 校验数据保存目录并返回规范化路径。 */
export async function validateDataRoot(dataRoot: string) {
  const { data } = await api.post<{ ok: boolean; needsMigration: boolean; message: string; normalizedPath: string }>('/settings/validate-data-root', { dataRoot })
  return data
}

/** 请求后端打开当前数据根目录。 */
export async function openDataRoot() {
  const { data } = await api.post<{ ok: boolean; path: string }>('/settings/open-data-root')
  return data
}

/** 请求后端弹出系统目录选择器。 */
export async function selectDataRoot() {
  const { data } = await api.post<{ ok: boolean; path: string }>('/settings/select-data-root')
  return data
}
