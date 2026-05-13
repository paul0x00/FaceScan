import axios from 'axios'
import type { Patient, PatientForm, ScanResult } from '../types'

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
  const { data } = await api.post<{ id: number }>('/patients', form)
  return data.id
}

export async function updatePatient(id: number, form: PatientForm) {
  const { data } = await api.put<{ ok: boolean }>(`/patients/${id}`, form)
  return data.ok
}

export async function createOrder(patientId: number) {
  const { data } = await api.post<{ id: number }>('/orders', { patientId })
  return data.id
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

export async function packageData(patientId: number) {
  const { data } = await api.post<{ ok: boolean; message: string }>('/export/package', { patientId })
  return data
}

export async function uploadData(patientId: number) {
  const { data } = await api.post<{ ok: boolean; message: string }>('/export/upload', { patientId })
  return data
}

