<script setup lang="ts">
import { computed, onBeforeUnmount, onMounted, ref } from 'vue'
import { useRoute, useRouter } from 'vue-router'
import { Camera, Pause } from 'lucide-vue-next'
import { ElMessage } from 'element-plus'
import StepHeader from '../components/StepHeader.vue'
import { capture, createOrder, startCamera, stopCamera } from '../api/client'

const route = useRoute()
const router = useRouter()
const patientId = computed(() => Number(route.params.id))
const running = ref(true)
const frameTick = ref(Date.now())
const orderId = ref<number>()
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
  orderId.value = await createOrder(patientId.value)
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

async function shoot() {
  const result = await capture(patientId.value, orderId.value)
  ElMessage.success(`同步采图完成，保存 ${result.paths.length} 张图像`)
  router.push(`/send/${patientId.value}`)
}
</script>

<template>
  <main class="workflow-page">
    <StepHeader active="shoot" />
    <section class="shoot-layout">
      <div class="camera-grid">
        <div
          v-for="camera in cameras"
          :key="camera.view"
          class="camera-panel"
          :class="camera.view"
        >
          <img :src="frameUrl(camera.view)" :alt="camera.label" />
          <span>{{ camera.label }}</span>
        </div>
      </div>
      <aside class="control-panel">操作控件<br />（设置亮度等）</aside>
      <footer class="shoot-actions">
        <button class="round-action filled" title="暂停/继续" type="button" @click="toggle">
          <Pause :size="30" />
        </button>
        <button class="round-action" title="同步拍摄" type="button" @click="shoot">
          <Camera :size="34" />
        </button>
      </footer>
    </section>
  </main>
</template>

