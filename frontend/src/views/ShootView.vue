<script setup lang="ts">
import { computed, onBeforeUnmount, onMounted, ref } from 'vue'
import { useRoute, useRouter } from 'vue-router'
import { Camera, Pause, Play, RotateCcw, SlidersHorizontal } from 'lucide-vue-next'
import { ElMessage } from 'element-plus'
import StepHeader from '../components/StepHeader.vue'
import { capture, createOrder, fetchOrders, startCamera, stopCamera } from '../api/client'

const route = useRoute()
const router = useRouter()
const patientId = computed(() => Number(route.params.id))
const routeOrderId = computed(() => Number(route.query.orderId || 0))
const running = ref(true)
const frameTick = ref(Date.now())
const orderId = ref<number>()
const capturing = ref(false)
const exposure = ref(42)
const brightness = ref(56)
let timer = 0

const cameras = [
  { view: 'left', label: '左侧相机图像' },
  { view: 'front', label: '正面相机图像' },
  { view: 'right', label: '右侧相机图像' },
  { view: 'bottom', label: '下方相机图像' }
]

function frameUrl(view: string) {
  return `/api/camera/frame?view=${view}&t=${frameTick.value}`
}

onMounted(async () => {
  await startCamera()
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
  }, 250)
})

onBeforeUnmount(async () => {
  window.clearInterval(timer)
  await stopCamera()
})

function toggle() {
  running.value = !running.value
}

function resetCameraParams() {
  exposure.value = 42
  brightness.value = 56
  ElMessage.success('相机参数已恢复默认')
}

async function shoot() {
  capturing.value = true
  try {
    const result = await capture(patientId.value, orderId.value)
    ElMessage.success(`同步采图完成，保存 ${result.paths.length} 张图像`)
    router.push(`/send/${patientId.value}`)
  } finally {
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
            <img :src="frameUrl(camera.view)" :alt="camera.label" />
            <span class="camera-name">{{ camera.label }}</span>
            <span class="camera-badge">{{ running ? '模拟预览' : '已暂停' }}</span>
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
