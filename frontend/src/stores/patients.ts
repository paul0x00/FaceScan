import { defineStore } from 'pinia'
import type { Patient } from '../types'
import { fetchPatients } from '../api/client'

export const usePatientStore = defineStore('patients', {
  state: () => ({
    patients: [] as Patient[],
    selectedId: 0,
    loading: false
  }),
  getters: {
    selected(state) {
      return state.patients.find((patient) => patient.id === state.selectedId) ?? state.patients[0]
    }
  },
  actions: {
    async load(params: { keyword?: string; date?: string } = {}) {
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

