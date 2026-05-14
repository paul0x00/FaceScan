<script setup lang="ts">
import { computed, onMounted, ref } from 'vue'
import { useRoute, useRouter } from 'vue-router'
import { ElMessage } from 'element-plus'
import { Archive, CheckCircle2, CloudUpload, FileCheck2 } from 'lucide-vue-next'
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
const packing = ref(false)
const uploading = ref(false)
const patient = computed(() => store.patients.find((item) => item.id === patientId.value))

onMounted(async () => {
  if (!store.patients.length) {
    await store.load()
  }
  scans.value = await fetchScans(patientId.value)
})

async function pack() {
  packing.value = true
  try {
    const result = await packageData(patientId.value)
    ElMessage.success(result.message)
  } finally {
    packing.value = false
  }
}

async function upload() {
  uploading.value = true
  try {
    const result = await uploadData(patientId.value)
    ElMessage.success(result.message)
  } finally {
    uploading.value = false
  }
}
</script>

<template>
  <main class="workflow-page">
    <StepHeader active="send" />
    <FormPanel>
      <header class="form-heading">
        <div>
          <span class="eyebrow">数据归档</span>
          <h1>扫描数据交付</h1>
        </div>
        <span class="status-pill ready">本地保存完成</span>
      </header>
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
        <button class="export-card" type="button" :disabled="packing" @click="pack">
          <Archive :size="22" />
          <span>{{ packing ? '打包中' : '压缩打包' }}</span>
          <small>生成本地交付包</small>
        </button>
        <button class="export-card" type="button" :disabled="uploading" @click="upload">
          <CloudUpload :size="24" />
          <span>{{ uploading ? '上传中' : '云端上传' }}</span>
          <small>接口预留，MVP 占位</small>
        </button>
        <span class="scan-count"><FileCheck2 :size="17" />已保存 {{ scans.length }} 张图像</span>
      </section>
      <footer class="panel-actions">
        <el-button class="primary-btn done-btn" @click="router.push('/browse')"><CheckCircle2 :size="17" />完成</el-button>
      </footer>
    </FormPanel>
  </main>
</template>
