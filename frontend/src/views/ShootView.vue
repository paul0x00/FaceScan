<script setup lang="ts">
import { computed, onBeforeUnmount, onMounted, ref } from 'vue'
import { useRoute, useRouter } from 'vue-router'
import { Camera, Pause, Play, RotateCcw, SlidersHorizontal } from 'lucide-vue-next'
import { ElLoading, ElMessage } from 'element-plus'
import StepHeader from '../components/StepHeader.vue'
import { capture, createOrder, fetchCameraControls, fetchOrders, reconstructOrder, startCamera, stopCamera, updateCameraControls } from '../api/client'
import type { CameraControlRange, CameraControlUpdate, CameraControls } from '../types'

const route = useRoute()
const router = useRouter()
/** 当前拍摄患者主键。 */
const patientId = computed(() => Number(route.params.id))
/** 路由指定的订单主键，缺失时自动选择或创建。 */
const routeOrderId = computed(() => Number(route.query.orderId || 0))
/** 前端预览刷新开关。 */
const running = ref(true)
/** 用于破坏图片缓存的帧时间戳。 */
const frameTick = ref(Date.now())
/** 当前拍摄订单主键。 */
const orderId = ref<number>()
/** 同步采图和点云生成进行中状态。 */
const capturing = ref(false)
/** 相机参数读取结果。 */
const cameraControls = ref<CameraControls>()
/** 相机参数提交状态。 */
const updatingControls = ref(false)
/** 参数控件列表。 */
const controlList = computed(() => cameraControls.value ? [
  cameraControls.value.exposure,
  cameraControls.value.gain
] : [])
/** 预览刷新定时器句柄。 */
let timer = 0

/** 四路相机预览配置。 */
const cameras = [
  { view: 'left', label: '左侧相机图像' },
  { view: 'front', label: '正面相机图像' },
  { view: 'right', label: '右侧相机图像' },
  { view: 'bottom', label: '下方相机图像' }
]

/** 构造相机预览 URL，并用时间戳刷新缓存。 */
function frameUrl() {
  return `/api/camera/frame?t=${frameTick.value}`
}

/** 启动相机、解析订单并启动预览刷新循环。 */
onMounted(async () => {
  try {
    await startCamera()
    await loadCameraControls()
  } catch (error) {
    running.value = false
    ElMessage.error(error instanceof Error ? error.message : '相机启动失败')
  }
  if (routeOrderId.value) {
    orderId.value = routeOrderId.value
  } else {
    const existingOrders = await fetchOrders(patientId.value)
    if (existingOrders.length) {
      orderId.value = existingOrders[0].id
    } else {
      const order = await createOrder(patientId.value)
      orderId.value = order.id
      ElMessage.success(`订单 ${order.orderNo} 已生成`)
    }
  }
  timer = window.setInterval(() => {
    if (running.value) {
      frameTick.value = Date.now()
    }
  }, 100)
})

/** 离开页面时停止刷新和后端相机预览。 */
onBeforeUnmount(async () => {
  window.clearInterval(timer)
  await stopCamera()
})

/** 暂停或恢复前端预览刷新。 */
function toggle() {
  running.value = !running.value
}

/** 读取设备当前相机参数。 */
async function loadCameraControls() {
  try {
    cameraControls.value = await fetchCameraControls()
  } catch (error) {
    ElMessage.warning(error instanceof Error ? error.message : '相机参数读取失败')
  }
}

/** 判断参数是否会被自动曝光接管。 */
function controlledByAutoExposure(control: CameraControlRange) {
  return Boolean(cameraControls.value?.autoExposure && (control.key === 'exposure' || control.key === 'gain'))
}

/** 参数控件禁用状态。 */
function controlDisabled(control: CameraControlRange) {
  return updatingControls.value || !control.supported || !control.writable || controlledByAutoExposure(control)
}

/** 提交单个数值参数。 */
async function applyCameraControl(control: CameraControlRange, value: number | number[]) {
  const nextValue = Array.isArray(value) ? value[0] : value
  const update: CameraControlUpdate = {}
  if (control.key === 'exposure') update.exposure = nextValue
  if (control.key === 'gain') update.gain = nextValue
  if ((control.key === 'exposure' || control.key === 'gain') && cameraControls.value?.autoExposure) {
    update.autoExposure = false
  }
  await saveCameraControls(update)
}

/** 切换自动曝光。 */
async function applyAutoExposure(value: boolean | string | number) {
  await saveCameraControls({ autoExposure: Boolean(value) })
}

/** 写入相机参数并刷新后端返回的设备状态。 */
async function saveCameraControls(update: CameraControlUpdate) {
  updatingControls.value = true
  try {
    cameraControls.value = await updateCameraControls(update)
    ElMessage.success('相机参数已更新')
  } catch (error) {
    await loadCameraControls()
    ElMessage.error(error instanceof Error ? error.message : '相机参数更新失败')
  } finally {
    updatingControls.value = false
  }
}

/** 恢复相机参数默认值。 */
async function resetCameraParams() {
  if (!cameraControls.value) return
  const update: CameraControlUpdate = {}
  if (cameraControls.value.autoExposureSupported && cameraControls.value.autoExposureWritable) {
    update.autoExposure = true
  }
  for (const control of controlList.value) {
    if (!control.supported || !control.writable) continue
    if (control.key === 'exposure') update.exposure = control.defaultValue
    if (control.key === 'gain') update.gain = control.defaultValue
  }
  await saveCameraControls(update)
}

/** 执行同步采图、点云重建，并进入点云查看页。 */
async function shoot() {
  capturing.value = true
  const loading = ElLoading.service({
    lock: true,
    text: '同步采图完成后正在生成彩色点云...',
    background: 'rgba(238, 242, 245, 0.74)'
  })
  try {
    const result = await capture(patientId.value, orderId.value)
    const pointCloud = await reconstructOrder(result.orderId)
    ElMessage.success(`点云生成完成，${pointCloud.pointCount} 个点`)
    router.push({
      path: `/pointcloud/${patientId.value}`,
      query: {
        orderId: String(result.orderId),
        plyPath: pointCloud.plyPath
      }
    })
  } catch (error) {
    ElMessage.error(error instanceof Error ? error.message : '点云生成失败')
  } finally {
    loading.close()
    capturing.value = false
  }
}
</script>

<template>
  <main class="workflow-page">
    <StepHeader active="shoot" />
    <section class="shoot-layout">
      <div class="shoot-main">
        <div class="camera-grid">
          <div
            v-for="camera in cameras"
            :key="camera.view"
            class="camera-panel"
            :class="camera.view"
          >
            <img :src="frameUrl()" :alt="camera.label" />
            <span class="camera-name">{{ camera.label }}</span>
            <span class="camera-badge">{{ running ? '实时彩色流' : '已暂停' }}</span>
          </div>
        </div>
        <aside class="control-panel">
          <header>
            <SlidersHorizontal :size="18" />
            <span>相机参数</span>
          </header>
          <label v-if="cameraControls?.autoExposureSupported" class="control-switch-row">
            <span>自动曝光</span>
            <el-switch
              v-model="cameraControls.autoExposure"
              :disabled="updatingControls || !cameraControls.autoExposureWritable"
              @change="applyAutoExposure"
            />
          </label>
          <label
            v-for="control in controlList"
            :key="control.key"
            class="control-slider-row"
            :class="{ disabled: controlDisabled(control) }"
          >
            <span class="control-label">
              {{ control.label }}
              <strong>{{ control.supported ? control.value : '不支持' }}</strong>
            </span>
            <el-slider
              v-model="control.value"
              :min="control.min"
              :max="control.max"
              :step="control.step"
              :disabled="controlDisabled(control)"
              @change="(value: number | number[]) => applyCameraControl(control, value)"
            />
          </label>
          <button class="mini-action" type="button" @click="resetCameraParams">
            <RotateCcw :size="15" />恢复默认
          </button>
        </aside>
      </div>
      <footer class="shoot-actions">
        <div class="shoot-action-cluster">
          <button class="shoot-action-btn" title="暂停/继续" type="button" :aria-pressed="!running" @click="toggle">
            <Pause v-if="running" :size="28" />
            <Play v-else :size="28" />
          </button>
          <button class="shoot-action-btn primary" title="同步拍摄" type="button" :disabled="capturing" @click="shoot">
            <Camera :size="26" />
            <span>同步拍摄</span>
          </button>
        </div>
      </footer>
    </section>
  </main>
</template>
