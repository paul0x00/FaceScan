<script setup lang="ts">
import { computed, onMounted, ref } from 'vue'
import { useRoute, useRouter } from 'vue-router'
import { ElMessage } from 'element-plus'
import FormPanel from '../components/FormPanel.vue'
import StepHeader from '../components/StepHeader.vue'
import { fetchScans, packageData, uploadData } from '../api/client'
import { usePatientStore } from '../stores/patients'
import type { ScanResult } from '../types'

const route = useRoute()
const router = useRouter()
const store = usePatientStore()
const patientId = computed(() => Number(route.params.id))
const scans = ref<ScanResult[]>([])
const patient = computed(() => store.patients.find((item) => item.id === patientId.value))

onMounted(async () => {
  if (!store.patients.length) {
    await store.load()
  }
  scans.value = await fetchScans(patientId.value)
})

async function pack() {
  const result = await packageData(patientId.value)
  ElMessage.success(result.message)
}

async function upload() {
  const result = await uploadData(patientId.value)
  ElMessage.success(result.message)
}
</script>

<template>
  <main class="workflow-page">
    <StepHeader active="send" />
    <FormPanel>
      <el-form class="send-form" label-position="top">
        <el-form-item label="患者编号">
          <el-input :model-value="patient?.patientNo" readonly />
        </el-form-item>
        <el-form-item label="姓名">
          <el-input :model-value="patient?.name" readonly />
        </el-form-item>
        <el-form-item label="数据格式">
          <el-select model-value="MVP图像包" disabled>
            <el-option label="MVP图像包" value="MVP图像包" />
          </el-select>
        </el-form-item>
        <el-form-item label="主治医师">
          <el-select :model-value="patient?.doctor" disabled>
            <el-option :label="patient?.doctor || '主治医师'" :value="patient?.doctor || ''" />
          </el-select>
        </el-form-item>
        <el-form-item class="wide" label="备注">
          <el-input :model-value="patient?.remark" readonly />
        </el-form-item>
      </el-form>

      <div class="send-divider" />
      <section class="send-summary">
        <el-button class="strong-btn" @click="pack">压缩打包</el-button>
        <el-button class="strong-btn" @click="upload">云端上传</el-button>
        <span class="scan-count">已保存 {{ scans.length }} 张图像</span>
      </section>
      <footer class="panel-actions">
        <el-button class="strong-btn done-btn" @click="router.push('/browse')">完成</el-button>
      </footer>
    </FormPanel>
  </main>
</template>

