<script setup lang="ts">
import { computed, ref } from 'vue'
import { Camera, Check, Crop, Home, LogOut, Settings, X } from 'lucide-vue-next'
import { ElMessage } from 'element-plus'
import { useRoute, useRouter } from 'vue-router'

const router = useRouter()
const route = useRoute()
const selecting = ref(false)
const screenshotDataUrl = ref('')
const selectionStart = ref<{ x: number; y: number }>()
const selectionEnd = ref<{ x: number; y: number }>()
const screenshotImage = ref<HTMLImageElement>()
const draggingSelection = ref(false)
let screenshotCanvas: HTMLCanvasElement | null = null

const selectionRect = computed(() => {
  if (!selectionStart.value || !selectionEnd.value) return undefined
  const left = Math.min(selectionStart.value.x, selectionEnd.value.x)
  const top = Math.min(selectionStart.value.y, selectionEnd.value.y)
  const width = Math.abs(selectionStart.value.x - selectionEnd.value.x)
  const height = Math.abs(selectionStart.value.y - selectionEnd.value.y)
  return { left, top, width, height }
})

/** 通过浏览器屏幕捕获 API 获取当前标签页优先的图像帧。 */
async function captureDisplayCanvas() {
  if (!navigator.mediaDevices?.getDisplayMedia) {
    ElMessage.warning('当前浏览器不支持网页截图，请使用系统截图工具')
    return undefined
  }

  const options: DisplayMediaStreamOptions & { preferCurrentTab?: boolean } = {
    video: { displaySurface: 'browser' },
    audio: false,
    preferCurrentTab: true
  }
  const stream = await navigator.mediaDevices.getDisplayMedia(options)
  try {
    const video = document.createElement('video')
    video.srcObject = stream
    video.muted = true
    await new Promise<void>((resolve) => {
      video.onloadedmetadata = () => resolve()
    })
    await video.play()

    const canvas = document.createElement('canvas')
    canvas.width = video.videoWidth
    canvas.height = video.videoHeight
    const context = canvas.getContext('2d')
    context?.drawImage(video, 0, 0)
    return canvas
  } finally {
    stream.getTracks().forEach((track) => track.stop())
  }
}

/** 下载 PNG 画布。 */
function downloadCanvas(canvas: HTMLCanvasElement, suffix = 'tab') {
  const link = document.createElement('a')
  link.download = `facescan-${suffix}-${new Date().toISOString().replace(/[:.]/g, '-')}.png`
  link.href = canvas.toDataURL('image/png')
  link.click()
}

/** 截取浏览器当前标签页并下载为 PNG。 */
async function takeTabScreenshot() {
  try {
    const canvas = await captureDisplayCanvas()
    if (!canvas) return
    downloadCanvas(canvas)
    ElMessage.success('截图已生成')
  } catch (error) {
    ElMessage.info('截图已取消')
  }
}

/** 进入框选截图模式。 */
async function takeSelectionScreenshot() {
  try {
    const canvas = await captureDisplayCanvas()
    if (!canvas) return
    screenshotCanvas = canvas
    screenshotDataUrl.value = canvas.toDataURL('image/png')
    selectionStart.value = undefined
    selectionEnd.value = undefined
    selecting.value = true
  } catch (error) {
    ElMessage.info('截图已取消')
  }
}

/** 开始拖拽选区。 */
function startSelection(event: PointerEvent) {
  if (!selecting.value) return
  draggingSelection.value = true
  selectionStart.value = { x: event.clientX, y: event.clientY }
  selectionEnd.value = { x: event.clientX, y: event.clientY }
  ;(event.currentTarget as HTMLElement).setPointerCapture(event.pointerId)
}

/** 更新拖拽选区。 */
function updateSelection(event: PointerEvent) {
  if (!draggingSelection.value || !selectionStart.value) return
  selectionEnd.value = { x: event.clientX, y: event.clientY }
}

/** 结束拖拽选区。 */
function finishSelectionDrag(event: PointerEvent) {
  if (draggingSelection.value && selectionStart.value) {
    selectionEnd.value = { x: event.clientX, y: event.clientY }
  }
  draggingSelection.value = false
  const target = event.currentTarget as HTMLElement
  if (target.hasPointerCapture(event.pointerId)) {
    target.releasePointerCapture(event.pointerId)
  }
}

/** 关闭框选截图模式。 */
function cancelSelection() {
  selecting.value = false
  draggingSelection.value = false
  screenshotDataUrl.value = ''
  screenshotCanvas = null
}

/** 裁剪选区并下载 PNG。 */
function confirmSelection() {
  const rect = selectionRect.value
  const image = screenshotImage.value
  if (!rect || !image || !screenshotCanvas || rect.width < 4 || rect.height < 4) {
    ElMessage.warning('请先框选截图区域')
    return
  }

  const imageRect = image.getBoundingClientRect()
  const left = Math.max(rect.left, imageRect.left)
  const top = Math.max(rect.top, imageRect.top)
  const right = Math.min(rect.left + rect.width, imageRect.right)
  const bottom = Math.min(rect.top + rect.height, imageRect.bottom)
  const width = right - left
  const height = bottom - top
  if (width < 4 || height < 4) {
    ElMessage.warning('请先框选截图区域')
    return
  }

  const scaleX = screenshotCanvas.width / imageRect.width
  const scaleY = screenshotCanvas.height / imageRect.height
  const cropCanvas = document.createElement('canvas')
  cropCanvas.width = Math.round(width * scaleX)
  cropCanvas.height = Math.round(height * scaleY)
  const context = cropCanvas.getContext('2d')
  context?.drawImage(
    screenshotCanvas,
    Math.round((left - imageRect.left) * scaleX),
    Math.round((top - imageRect.top) * scaleY),
    cropCanvas.width,
    cropCanvas.height,
    0,
    0,
    cropCanvas.width,
    cropCanvas.height
  )
  downloadCanvas(cropCanvas, 'selection')
  cancelSelection()
  ElMessage.success('截图已生成')
}

/** 清除本地 token 并返回登录页。 */
function logout() {
  localStorage.removeItem('facescan.token')
  ElMessage.success('已退出登录')
  router.push('/login')
}

/** 打开设置页；从首页进入时允许编辑数据目录。 */
function openSettings() {
  router.push({
    path: '/settings',
    query: route.path === '/browse' ? { from: 'browse' } : {}
  })
}
</script>

<template>
  <div class="window-icons" aria-label="页面工具栏">
    <button class="toolbar-btn" data-tooltip="截图当前标签页" aria-label="截图当前标签页" type="button" @click="takeTabScreenshot">
      <Camera :size="21" />
    </button>
    <button class="toolbar-btn" data-tooltip="框选截图" aria-label="框选截图" type="button" @click="takeSelectionScreenshot">
      <Crop :size="21" />
    </button>
    <button class="toolbar-btn" data-tooltip="返回工作台" aria-label="返回工作台" type="button" @click="router.push('/browse')">
      <Home :size="21" />
    </button>
    <button class="toolbar-btn" data-tooltip="系统设置" aria-label="系统设置" type="button" @click="openSettings">
      <Settings :size="21" />
    </button>
    <button class="toolbar-btn ghost-danger" data-tooltip="退出登录" aria-label="退出登录" type="button" @click="logout">
      <LogOut :size="21" />
    </button>
  </div>
  <teleport to="body">
    <div v-if="selecting" class="screenshot-overlay">
      <img ref="screenshotImage" class="screenshot-image" :src="screenshotDataUrl" alt="" draggable="false" />
      <div
        class="screenshot-selection-layer"
        :class="{ dragging: draggingSelection }"
        @pointerdown="startSelection"
        @pointermove="updateSelection"
        @pointerup="finishSelectionDrag"
        @pointercancel="finishSelectionDrag"
      >
        <div
          v-if="selectionRect"
          class="screenshot-selection"
          :style="{
            left: `${selectionRect.left}px`,
            top: `${selectionRect.top}px`,
            width: `${selectionRect.width}px`,
            height: `${selectionRect.height}px`
          }"
        />
      </div>
      <div class="screenshot-actions">
        <button class="screenshot-action-btn" data-tooltip="取消" aria-label="取消" type="button" @click="cancelSelection">
          <X :size="22" />
        </button>
        <button class="screenshot-action-btn primary" data-tooltip="保存" aria-label="保存" type="button" @click="confirmSelection">
          <Check :size="22" />
        </button>
      </div>
    </div>
  </teleport>
</template>
