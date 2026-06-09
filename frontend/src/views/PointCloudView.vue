<script setup lang="ts">
import '@kitware/vtk.js/Rendering/Profiles/Geometry'

import vtkActor from '@kitware/vtk.js/Rendering/Core/Actor'
import vtkGenericRenderWindow from '@kitware/vtk.js/Rendering/Misc/GenericRenderWindow'
import vtkMapper from '@kitware/vtk.js/Rendering/Core/Mapper'
import vtkPLYReader from '@kitware/vtk.js/IO/Geometry/PLYReader'
import { Box, Grid3X3, Image, RotateCcw } from 'lucide-vue-next'
import { computed, nextTick, onBeforeUnmount, onMounted, ref } from 'vue'
import { useRoute } from 'vue-router'
import StepHeader from '../components/StepHeader.vue'
import { fetchScans, pointCloudFileUrl } from '../api/client'
import type { ScanResult } from '../types'

const route = useRoute()
/** 当前患者主键。 */
const patientId = computed(() => Number(route.params.id))
/** 当前订单主键。 */
const orderId = computed(() => Number(route.query.orderId || 0))
/** 路由直接传入的 PLY 路径。 */
const routePlyPath = computed(() => String(route.query.plyPath || ''))
/** 患者扫描记录。 */
const scans = ref<ScanResult[]>([])
/** 当前加载的 PLY 文件路径。 */
const plyPath = ref('')
/** vtk.js 读取到的点数量。 */
const pointCount = ref(0)
/** 点云加载状态。 */
const loading = ref(true)
/** 点云加载错误信息。 */
const error = ref('')
/** vtk.js 渲染容器。 */
const viewerEl = ref<HTMLElement | null>(null)

/** vtk.js 通用渲染窗口实例。 */
let genericWindow: any = null
/** vtk.js 渲染器实例。 */
let renderer: any = null
/** vtk.js 渲染窗口实例。 */
let renderWindow: any = null
/** vtk.js mapper，用于切换纹理显示。 */
let mapper: any = null
/** vtk.js actor，用于刷新显示。 */
let actor: any = null
/** 当前点云包围盒。 */
let pointBounds: number[] | null = null
/** 容器尺寸变化监听器。 */
let resizeObserver: ResizeObserver | null = null
/** 六视图弹窗根节点。 */
const viewPanelRef = ref<HTMLElement | null>(null)

/** 当前 PLY 文件读取 URL。 */
const fileUrl = computed(() => (plyPath.value ? pointCloudFileUrl(plyPath.value) : ''))
/** 左上角状态文本。 */
const titleText = computed(() => (pointCount.value ? `${pointCount.value} 点` : '等待加载'))
/** 点云颜色显示开关。 */
const textureVisible = ref(true)
/** 六视图浮层开关。 */
const viewPanelOpen = ref(false)
type StandardView = 'back' | 'down' | 'front' | 'left' | 'right' | 'up'

/** 当前选中的标准视角。 */
const activeView = ref<StandardView>('front')
/** 六视图列表。 */
const viewOptions = [
  { key: 'back', label: '后视图' },
  { key: 'down', label: '下视图' },
  { key: 'front', label: '前视图' },
  { key: 'left', label: '左视图' },
  { key: 'right', label: '右视图' },
  { key: 'up', label: '上视图' }
] as const

/** 解析当前订单点云路径并初始化 vtk.js 加载流程。 */
onMounted(async () => {
  document.addEventListener('pointerdown', handleDocumentPointerDown)
  scans.value = await fetchScans(patientId.value)
  const latest = scans.value.find((item) => item.plyPath && (!orderId.value || item.orderId === orderId.value))
  plyPath.value = routePlyPath.value || latest?.plyPath || ''
  if (!plyPath.value) {
    error.value = '当前订单还没有生成点云，无法进入三维处理阶段'
    loading.value = false
    return
  }
  await nextTick()
  await loadPointCloud()
})

/** 销毁 vtk.js 资源和尺寸监听器。 */
onBeforeUnmount(() => {
  document.removeEventListener('pointerdown', handleDocumentPointerDown)
  resizeObserver?.disconnect()
  resizeObserver = null
  genericWindow?.delete()
  mapper = null
  actor = null
  pointBounds = null
})

/** 加载 PLY 点云并创建 vtk.js 点渲染管线。 */
async function loadPointCloud() {
  if (!viewerEl.value || !plyPath.value) return
  loading.value = true
  error.value = ''
  try {
    genericWindow = vtkGenericRenderWindow.newInstance({
      background: [0.96, 0.98, 0.99],
      listenWindowResize: true
    })
    genericWindow.setContainer(viewerEl.value)
    renderer = genericWindow.getRenderer()
    renderWindow = genericWindow.getRenderWindow()

    const reader = vtkPLYReader.newInstance()
    await reader.setUrl(fileUrl.value, { binary: false })
    mapper = vtkMapper.newInstance()
    mapper.setInputConnection(reader.getOutputPort())
    mapper.setScalarVisibility(true)
    mapper.setColorModeToDirectScalars()
    mapper.setScalarModeToUsePointData()

    actor = vtkActor.newInstance()
    actor.setMapper(mapper)
    actor.getProperty().setRepresentationToPoints()
    actor.getProperty().setPointSize(3)
    renderer.addActor(actor)
    renderer.resetCamera()
    renderWindow.render()

    const data = reader.getOutputData(0)
    pointCount.value = data?.getPoints?.().getNumberOfPoints?.() || 0
    pointBounds = data?.getBounds?.() || null
    resizeObserver = new ResizeObserver(() => {
      genericWindow?.resize()
      renderWindow?.render()
    })
    resizeObserver.observe(viewerEl.value)
  } catch (err) {
    error.value = err instanceof Error ? err.message : '点云加载失败'
  } finally {
    loading.value = false
  }
}

/** 将 vtk.js 相机重置到完整点云视角。 */
function resetCamera() {
  if (pointBounds) {
    applyView('front')
    return
  }
  activeView.value = 'front'
  renderer?.resetCamera()
  renderWindow?.render()
}

/** 切换点云颜色显示。 */
function toggleTexture() {
  textureVisible.value = !textureVisible.value
  if (!mapper) return
  if (textureVisible.value) {
    mapper.setLookupTable(null)
    mapper.setColorModeToDirectScalars()
    mapper.setScalarVisibility(true)
    actor?.getProperty?.().setColor(1, 1, 1)
  } else {
    mapper.setLookupTable(null)
    mapper.setScalarVisibility(false)
    actor?.getProperty?.().setColor(0.28, 0.28, 0.28)
  }
  renderWindow?.render()
}

/** 展开或收起六视图工具面板。 */
function toggleViewPanel() {
  viewPanelOpen.value = !viewPanelOpen.value
}

/** 点击六视图弹窗外部后关闭面板。 */
function handleDocumentPointerDown(event: PointerEvent) {
  if (!viewPanelOpen.value) return
  const target = event.target as HTMLElement | null
  if (!target) return
  if (target.closest('.pointcloud-toolbtn--views')) return
  if (viewPanelRef.value?.contains(target)) return
  viewPanelOpen.value = false
}

/** 将相机切换到固定视角。 */
function applyView(view: StandardView) {
  const camera = renderer?.getActiveCamera?.()
  if (!camera || !pointBounds) return
  const centerX = (pointBounds[0] + pointBounds[1]) / 2
  const centerY = (pointBounds[2] + pointBounds[3]) / 2
  const centerZ = (pointBounds[4] + pointBounds[5]) / 2
  const sizeX = Math.max(1, pointBounds[1] - pointBounds[0])
  const sizeY = Math.max(1, pointBounds[3] - pointBounds[2])
  const sizeZ = Math.max(1, pointBounds[5] - pointBounds[4])
  const distance = Math.max(sizeX, sizeY, sizeZ) * 2.2

  camera.setFocalPoint(centerX, centerY, centerZ)
  if (view === 'front') {
    camera.setPosition(centerX, centerY, centerZ + distance)
    camera.setViewUp(0, 1, 0)
  } else if (view === 'back') {
    camera.setPosition(centerX, centerY, centerZ - distance)
    camera.setViewUp(0, 1, 0)
  } else if (view === 'left') {
    camera.setPosition(centerX - distance, centerY, centerZ)
    camera.setViewUp(0, 1, 0)
  } else if (view === 'right') {
    camera.setPosition(centerX + distance, centerY, centerZ)
    camera.setViewUp(0, 1, 0)
  } else if (view === 'up') {
    camera.setPosition(centerX, centerY + distance, centerZ)
    camera.setViewUp(0, 0, -1)
  } else if (view === 'down') {
    camera.setPosition(centerX, centerY - distance, centerZ)
    camera.setViewUp(0, 0, 1)
  } else {
    renderer?.resetCamera()
  }
  activeView.value = view
  renderer?.resetCameraClippingRange()
  renderWindow?.render()
  viewPanelOpen.value = false
}
</script>

<template>
  <main class="workflow-page pointcloud-page">
    <StepHeader active="process" />
    <section class="pointcloud-workspace">
      <section class="pointcloud-stage pointcloud-stage--workspace">
        <div ref="viewerEl" class="pointcloud-viewer" />
        <div class="pointcloud-stage-pill">
          <span class="status-pill ready"><Box :size="14" />{{ titleText }} · 彩色点云显示</span>
        </div>
        <div class="pointcloud-toolrail">
          <button class="pointcloud-toolbtn soft-btn" type="button" :disabled="loading || !!error" @click="resetCamera">
            <RotateCcw :size="17" />
            <span>重置视角</span>
          </button>
          <button class="pointcloud-toolbtn soft-btn" type="button" :class="{ active: textureVisible }" :disabled="loading || !!error" @click="toggleTexture">
            <Image :size="17" />
            <span>{{ textureVisible ? '纹理显示' : '纹理关闭' }}</span>
          </button>
          <button class="pointcloud-toolbtn pointcloud-toolbtn--views soft-btn" type="button" :class="{ active: viewPanelOpen }" :disabled="loading || !!error" @click="toggleViewPanel">
            <Grid3X3 :size="17" />
            <span>六视图</span>
          </button>
        </div>
        <div v-if="viewPanelOpen" ref="viewPanelRef" class="pointcloud-viewpanel">
          <button
            v-for="view in viewOptions"
            :key="view.key"
            class="pointcloud-viewcell"
            :class="{ active: activeView === view.key }"
            type="button"
            :disabled="loading || !!error"
            @click="applyView(view.key)"
            :title="view.label"
            :aria-label="view.label"
          >
            <svg class="cube-view-icon" :class="[`cube-view-icon--${view.key}`]" viewBox="0 0 64 64" aria-hidden="true" focusable="false">
              <path class="cube-view-fill cube-fill-back" d="M25 10H51V36H25Z" />
              <path class="cube-view-fill cube-fill-down" d="M13 50L25 38H51L39 50Z" />
              <path class="cube-view-fill cube-fill-front" d="M13 22H39V50H13Z" />
              <path class="cube-view-fill cube-fill-left" d="M13 22L25 10V38L13 50Z" />
              <path class="cube-view-fill cube-fill-right" d="M39 22L51 10V36L39 50Z" />
              <path class="cube-view-fill cube-fill-up" d="M13 22L25 10H51L39 22Z" />
              <path class="cube-view-wire" d="M13 22H39V50H13V22Z" />
              <path class="cube-view-wire" d="M13 22L25 10H51V36L39 50" />
              <path class="cube-view-wire" d="M39 22L51 10" />
              <path class="cube-view-wire" d="M39 50V22" />
              <path class="cube-view-wire" d="M51 36H25V10" />
              <path class="cube-view-wire" d="M13 50L25 38H51" />
            </svg>
          </button>
        </div>
        <div class="pointcloud-hintbar">
          <span>拖拽旋转</span>
          <span>滚轮缩放</span>
          <span>Shift 平移</span>
        </div>
        <div v-if="loading" class="pointcloud-overlay">点云加载中</div>
        <div v-else-if="error" class="pointcloud-overlay error">{{ error }}</div>
      </section>
    </section>
  </main>
</template>
