import { defineStore } from 'pinia'
import type { Patient } from '../types'
import { fetchPatients } from '../api/client'

/** 患者列表的共享状态仓库。 */
export const usePatientStore = defineStore('patients', {
  state: () => ({
    /** 当前检索结果。 */
    patients: [] as Patient[],
    /** 当前选中患者主键。 */
    selectedId: 0,
    /** 患者列表加载状态。 */
    loading: false
  }),
  getters: {
    /** 当前选中患者；未显式选择时返回列表第一项。 */
    selected(state) {
      return state.patients.find((patient) => patient.id === state.selectedId) ?? state.patients[0]
    }
  },
  actions: {
    /** 按条件刷新患者列表，并在必要时初始化选中患者。 */
    async load(params: { keyword?: string; date?: string; startDate?: string; endDate?: string } = {}) {
      this.loading = true
      try {
        this.patients = await fetchPatients(params)
        if (!this.selectedId && this.patients.length) {
          this.selectedId = this.patients[0].id
        }
      } finally {
        this.loading = false
      }
    }
  }
})
