<script setup lang="ts">
import { computed, onMounted, ref } from 'vue'
import { useRoute, useRouter } from 'vue-router'
import { ElMessage } from 'element-plus'
import { Archive, CheckCircle2, CloudUpload, FileCheck2, FolderOpen } from 'lucide-vue-next'
import FormPanel from '../components/FormPanel.vue'
import StepHeader from '../components/StepHeader.vue'
import { fetchScans, openOrderFolder, packageData, uploadData } from '../api/client'
import { usePatientStore } from '../stores/patients'
import type { ScanResult } from '../types'

const route = useRoute()
const router = useRouter()
const store = usePatientStore()
/** 当前交付患者主键。 */
const patientId = computed(() => Number(route.params.id))
/** 路由指定的订单主键。 */
const routeOrderId = computed(() => Number(route.query.orderId || 0))
/** 患者扫描记录集合。 */
const scans = ref<ScanResult[]>([])
/** 本地打包进行中状态。 */
const packing = ref(false)
/** 云端上传占位接口进行中状态。 */
const uploading = ref(false)
/** 当前患者资料。 */
const patient = computed(() => store.patients.find((item) => item.id === patientId.value))
/** 用于交付操作的最近订单主键。 */
const latestOrderId = computed(() => routeOrderId.value || scans.value.find((item) => item.orderId)?.orderId || 0)
/** 已保存图像数量统计。 */
const imageCount = computed(() => scans.value.reduce((sum, item) => sum + (item.imagePaths?.length || (item.plyPath ? 0 : 1)), 0))
/** 已生成点云的扫描记录。 */
const pointClouds = computed(() => scans.value.filter((item) => item.plyPath))
/** 当前订单最近的 PLY 路径。 */
const latestPlyPath = computed(() => pointClouds.value.find((item) => !latestOrderId.value || item.orderId === latestOrderId.value)?.plyPath || '')

/** 加载患者和扫描记录。 */
onMounted(async () => {
  await store.load()
  store.selectedId = patientId.value
  scans.value = await fetchScans(patientId.value)
})

/** 打包当前订单交付数据。 */
async function pack() {
  packing.value = true
  try {
    const result = await packageData(patientId.value, latestOrderId.value)
    ElMessage.success(`${result.message}：${result.path}`)
  } finally {
    packing.value = false
  }
}

/** 调用 MVP 云端上传占位接口。 */
async function upload() {
  uploading.value = true
  try {
    const result = await uploadData(patientId.value)
    ElMessage.success(result.message)
  } finally {
    uploading.value = false
  }
}

/** 请求后端打开当前订单目录。 */
async function openFolder() {
  if (!latestOrderId.value) return
  const result = await openOrderFolder(latestOrderId.value)
  if (result.ok) {
    ElMessage.success(`已打开目录：${result.path}`)
  }
}

/** 阻止只读字段聚焦，保持只读展示的交互一致性。 */
function preventReadonlyFocus(event: Event) {
  event.preventDefault()
  const target = event.target as HTMLElement | null
  target?.blur()
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
          <el-input class="readonly-control" :model-value="patient?.patientNo" readonly tabindex="-1" title="不可编辑" @mousedown.capture="preventReadonlyFocus" @focusin="preventReadonlyFocus" />
        </el-form-item>
        <el-form-item label="姓名">
          <el-input class="readonly-control" :model-value="patient?.name" readonly tabindex="-1" title="不可编辑" @mousedown.capture="preventReadonlyFocus" @focusin="preventReadonlyFocus" />
        </el-form-item>
        <el-form-item label="数据格式">
          <el-select model-value="同步图片 + 彩色PLY点云" disabled>
            <el-option label="同步图片 + 彩色PLY点云" value="同步图片 + 彩色PLY点云" />
          </el-select>
        </el-form-item>
        <el-form-item label="主治医师">
          <el-select :model-value="patient?.doctor" disabled>
            <el-option :label="patient?.doctor || '主治医师'" :value="patient?.doctor || ''" />
          </el-select>
        </el-form-item>
        <el-form-item class="wide" label="备注">
          <el-input class="readonly-control" :model-value="patient?.remark" readonly tabindex="-1" title="不可编辑" @mousedown.capture="preventReadonlyFocus" @focusin="preventReadonlyFocus" />
        </el-form-item>
      </el-form>

      <div class="send-divider" />
      <section class="send-summary">
        <button class="export-card" type="button" :disabled="packing || !latestOrderId" @click="pack">
          <Archive :size="22" />
          <span>{{ packing ? '打包中' : '压缩打包' }}</span>
          <small>生成本地交付包</small>
        </button>
        <button class="export-card" type="button" :disabled="uploading" @click="upload">
          <CloudUpload :size="24" />
          <span>{{ uploading ? '上传中' : '云端上传' }}</span>
          <small>接口预留，MVP 占位</small>
        </button>
        <span class="scan-count"><FileCheck2 :size="17" />{{ imageCount }} 张图像 / {{ pointClouds.length }} 个点云</span>
      </section>
      <section v-if="latestPlyPath" class="pointcloud-result">
        <div>
          <span>PLY 输出</span>
          <strong>{{ latestPlyPath }}</strong>
        </div>
        <button class="mini-action" type="button" @click="openFolder">
          <FolderOpen :size="15" />打开目录
        </button>
      </section>
      <footer class="panel-actions">
        <el-button class="primary-btn done-btn" @click="router.push('/browse')"><CheckCircle2 :size="17" />完成</el-button>
      </footer>
    </FormPanel>
  </main>
</template>
