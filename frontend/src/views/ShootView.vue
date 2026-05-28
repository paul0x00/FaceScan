<script setup lang="ts">
import { computed, onBeforeUnmount, onMounted, ref } from 'vue'
import { useRoute, useRouter } from 'vue-router'
import { Camera, Pause, Play, RotateCcw, SlidersHorizontal } from 'lucide-vue-next'
import { ElLoading, ElMessage } from 'element-plus'
import StepHeader from '../components/StepHeader.vue'
import { capture, createOrder, fetchOrders, reconstructOrder, startCamera, stopCamera } from '../api/client'

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
/** MVP 相机曝光参数占位值。 */
const exposure = ref(42)
/** MVP 相机亮度参数占位值。 */
const brightness = ref(56)
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

/** 恢复 MVP 相机参数占位控件默认值。 */
function resetCameraParams() {
  exposure.value = 42
  brightness.value = 56
  ElMessage.success('相机参数已恢复默认')
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
          <label>
            曝光
            <el-slider v-model="exposure" :min="0" :max="100" />
          </label>
          <label>
            亮度
            <el-slider v-model="brightness" :min="0" :max="100" />
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
