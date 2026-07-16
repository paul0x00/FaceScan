<script setup lang="ts">
import { computed, onBeforeUnmount, onMounted, ref } from 'vue'
import { useRoute, useRouter } from 'vue-router'
import { ArrowRight, Camera, Pause, Play, RotateCcw, SlidersHorizontal } from 'lucide-vue-next'
import { ElLoading, ElMessage } from 'element-plus'
import StepHeader from '../components/StepHeader.vue'
import { capture, createOrder, fetchCameraControls, fetchOrders, fetchScans, reconstructOrder, startCamera, stopCamera, updateCameraControls } from '../api/client'
import type { CameraControlRange, CameraControlUpdate, CameraControls } from '../types'

const previewFps = 20
const route = useRoute()
const router = useRouter()
/** 当前拍摄患者主键。 */
const patientId = computed(() => Number(route.params.id))
/** 路由指定的订单主键，缺失时自动选择或创建。 */
const routeOrderId = computed(() => Number(route.query.orderId || 0))
/** 前端预览刷新开关。 */
const running = ref(true)
/** 四路预览当前显示的单帧地址。 */
const frameUrls = ref<Record<string, string>>({})
/** 预览调度版本，用于让暂停前排队的刷新失效。 */
const previewGeneration = ref(0)
/** 当前拍摄订单主键。 */
const orderId = ref<number>()
/** 同步采图进行中状态。 */
const capturing = ref(false)
/** 点云生成和跳转进行中状态。 */
const reconstructing = ref(false)
/** 当前页面最近一次同步采图成功的订单主键。 */
const capturedOrderId = ref(0)
/** 当前订单已保存的采图数量。 */
const capturedImageCount = ref(0)
/** 相机参数读取结果。 */
const cameraControls = ref<CameraControls>()
/** 相机参数提交状态。 */
const updatingControls = ref(false)
/** 参数控件列表。 */
const controlList = computed(() => cameraControls.value ? [
  cameraControls.value.exposure,
  cameraControls.value.gain
] : [])
/** 四路相机预览配置。 */
const cameras = [
  { view: 'left', label: '左侧奥比中光彩色图' },
  { view: 'front', label: '正面海康威视彩色图' },
  { view: 'right', label: '右侧奥比中光彩色图' },
  { view: 'bottom', label: '下方奥比中光彩色图' }
]
/** 下一步需要完整四路采图后才允许进入。 */
const canGoNext = computed(() => capturedImageCount.value >= cameras.length)

/** 构造单帧预览 URL，并用时间戳破坏浏览器缓存。 */
function frameUrl(view: string) {
  return `/api/camera/frame?view=${encodeURIComponent(view)}&t=${Date.now()}`
}

/** 更新某一路预览地址。 */
function requestFrame(view: string) {
  frameUrls.value = {
    ...frameUrls.value,
    [view]: frameUrl(view)
  }
}

/** 在上一帧完成加载后再请求下一帧，避免轮询堆积请求。 */
function scheduleNextFrame(view: string) {
  if (!running.value) return
  const generation = previewGeneration.value
  window.setTimeout(() => {
    if (running.value && generation === previewGeneration.value) {
      requestFrame(view)
    }
  }, Math.round(1000 / previewFps))
}

/** 初始化所有预览地址。 */
function requestAllFrames() {
  previewGeneration.value += 1
  for (const camera of cameras) {
    requestFrame(camera.view)
  }
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
  await refreshCapturedImageCount()
  requestAllFrames()
})

/** 离开页面时停止刷新和后端相机预览。 */
onBeforeUnmount(async () => {
  await stopCamera()
})

/** 暂停或恢复前端预览刷新。 */
function toggle() {
  if (running.value) {
    previewGeneration.value += 1
    running.value = false
    return
  }
  running.value = true
  requestAllFrames()
}

/** 读取设备当前相机参数。 */
async function loadCameraControls() {
  try {
    cameraControls.value = await fetchCameraControls()
  } catch (error) {
    ElMessage.warning(error instanceof Error ? error.message : '相机参数读取失败')
  }
}

/** 刷新当前订单已经保存的采图数量。 */
async function refreshCapturedImageCount() {
  if (!orderId.value) {
    capturedImageCount.value = 0
    return
  }
  try {
    const scans = await fetchScans(patientId.value)
    const scan = scans.find((item) => item.orderId === orderId.value && item.imagePaths?.length)
    capturedImageCount.value = scan?.imagePaths?.length ?? 0
    if (capturedImageCount.value >= cameras.length) {
      capturedOrderId.value = orderId.value
    }
  } catch (error) {
    capturedImageCount.value = 0
    ElMessage.warning(error instanceof Error ? error.message : '采图状态读取失败')
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

/** 执行同步采图并保存订单图像。 */
async function shoot() {
  if (capturing.value) return
  previewGeneration.value += 1
  running.value = false
  capturing.value = true
  const loading = ElLoading.service({
    lock: true,
    text: '正在同步采图并保存图像...',
    background: 'rgba(238, 242, 245, 0.74)'
  })
  try {
    const result = await capture(patientId.value, orderId.value)
    orderId.value = result.orderId
    capturedOrderId.value = result.orderId
    capturedImageCount.value = result.paths.length
    ElMessage.success(`同步拍摄完成，已保存 ${result.paths.length} 张图像`)
  } catch (error) {
    capturedImageCount.value = 0
    ElMessage.error(error instanceof Error ? error.message : '同步拍摄失败')
  } finally {
    loading.close()
    capturing.value = false
  }
}

/** 根据已保存图像生成点云，并进入三维处理页。 */
async function goPointCloud() {
  const targetOrderId = capturedOrderId.value || orderId.value
  if (!targetOrderId || reconstructing.value || !canGoNext.value) return
  reconstructing.value = true
  const loading = ElLoading.service({
    lock: true,
    text: '正在生成彩色点云...',
    background: 'rgba(238, 242, 245, 0.74)'
  })
  try {
    const pointCloud = await reconstructOrder(targetOrderId)
    ElMessage.success(`点云生成完成，${pointCloud.pointCount} 个点`)
    router.push({
      path: `/pointcloud/${patientId.value}`,
      query: {
        orderId: String(targetOrderId),
        plyPath: pointCloud.plyPath
      }
    })
  } catch (error) {
    ElMessage.error(error instanceof Error ? error.message : '点云生成失败')
  } finally {
    loading.close()
    reconstructing.value = false
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
            <img :src="frameUrls[camera.view]" :alt="camera.label" @load="scheduleNextFrame(camera.view)" @error="scheduleNextFrame(camera.view)" />
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
          <button class="shoot-action-btn" :title="running ? '暂停预览' : '继续预览'" type="button" :aria-pressed="!running" @click="toggle">
            <Pause v-if="running" :size="22" />
            <Play v-else :size="22" />
            <span>{{ running ? '暂停' : '继续' }}</span>
          </button>
          <button class="shoot-action-btn primary" title="同步拍摄" type="button" :disabled="capturing" @click="shoot">
            <Camera :size="22" />
            <span>{{ capturing ? '保存中' : '同步拍摄' }}</span>
          </button>
          <button class="shoot-action-btn next" title="生成点云并进入下一步" type="button" :disabled="capturing || reconstructing || !orderId || !canGoNext" @click="goPointCloud">
            <ArrowRight :size="22" />
            <span>{{ reconstructing ? '处理中' : '下一步' }}</span>
          </button>
        </div>
      </footer>
    </section>
  </main>
</template>
