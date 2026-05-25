import axios from 'axios'
import type { AppSettings, Order, Patient, PatientForm, PointCloudResult, ScanResult } from '../types'

const api = axios.create({
  baseURL: '/api',
  timeout: 8000
})

api.interceptors.request.use((config) => {
  const token = localStorage.getItem('facescan.token')
  if (token) {
    config.headers.Authorization = `Bearer ${token}`
  }
  return config
})

export async function login(username: string, password: string) {
  const { data } = await api.post('/login', { username, password })
  localStorage.setItem('facescan.token', data.token)
  return data
}

export async function fetchPatients(params: { keyword?: string; date?: string }) {
  const { data } = await api.get<{ items: Patient[] }>('/patients', { params })
  return data.items
}

export async function createPatient(form: PatientForm) {
  const { data } = await api.post<{ id: number; orderId: number; patientNo: string; orderNo: string }>('/patients', form)
  return data
}

export async function updatePatient(id: number, form: PatientForm) {
  const { data } = await api.put<{ ok: boolean }>(`/patients/${id}`, form)
  return data.ok
}

export async function deletePatient(id: number) {
  const { data } = await api.delete<{ ok: boolean }>(`/patients/${id}`)
  return data.ok
}

export async function createOrder(patientId: number) {
  const { data } = await api.post<{ id: number; orderNo: string }>('/orders', { patientId })
  return data
}

export async function fetchOrders(patientId: number) {
  const { data } = await api.get<{ items: Order[] }>(`/patients/${patientId}/orders`)
  return data.items
}

export async function deleteOrder(id: number) {
  const { data } = await api.delete<{ ok: boolean }>(`/orders/${id}`)
  return data.ok
}

export async function openOrderFolder(id: number) {
  const { data } = await api.post<{ ok: boolean; path: string }>(`/orders/${id}/open-folder`)
  return data
}

export async function reconstructOrder(id: number, options: { columns?: number; rows?: number } = {}) {
  const { data } = await api.post<PointCloudResult>(`/orders/${id}/reconstruct`, {
    columns: options.columns ?? 96,
    rows: options.rows ?? 72
  })
  return data
}

export function pointCloudFileUrl(path: string) {
  return `/api/pointcloud/file?path=${encodeURIComponent(path)}`
}

export function imageFileUrl(path: string) {
  return `/api/files/image?path=${encodeURIComponent(path)}`
}

export async function startCamera() {
  await api.post('/camera/start')
}

export async function stopCamera() {
  await api.post('/camera/stop')
}

export async function capture(patientId: number, orderId?: number) {
  const { data } = await api.post<{ orderId: number; paths: string[] }>('/camera/capture', { patientId, orderId })
  return data
}

export async function fetchScans(patientId: number) {
  const { data } = await api.get<{ items: ScanResult[] }>(`/patients/${patientId}/scans`)
  return data.items
}

export async function packageData(patientId: number, orderId?: number) {
  const { data } = await api.post<{ ok: boolean; message: string; path: string }>('/export/package', { patientId, orderId })
  return data
}

export async function uploadData(patientId: number) {
  const { data } = await api.post<{ ok: boolean; message: string }>('/export/upload', { patientId })
  return data
}

export async function fetchSettings() {
  const { data } = await api.get<AppSettings>('/settings')
  return data
}

export async function saveSettings(settings: Pick<AppSettings, 'dataRoot'>) {
  const { data } = await api.put<AppSettings>('/settings', settings)
  return data
}

export async function validateDataRoot(dataRoot: string) {
  const { data } = await api.post<{ ok: boolean; needsMigration: boolean; message: string; normalizedPath: string }>('/settings/validate-data-root', { dataRoot })
  return data
}

export async function openDataRoot() {
  const { data } = await api.post<{ ok: boolean; path: string }>('/settings/open-data-root')
  return data
}

export async function selectDataRoot() {
  const { data } = await api.post<{ ok: boolean; path: string }>('/settings/select-data-root')
  return data
}
