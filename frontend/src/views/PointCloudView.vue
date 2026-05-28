<script setup lang="ts">
import '@kitware/vtk.js/Rendering/Profiles/Geometry'

import vtkActor from '@kitware/vtk.js/Rendering/Core/Actor'
import vtkGenericRenderWindow from '@kitware/vtk.js/Rendering/Misc/GenericRenderWindow'
import vtkMapper from '@kitware/vtk.js/Rendering/Core/Mapper'
import vtkPLYReader from '@kitware/vtk.js/IO/Geometry/PLYReader'
import { ArrowLeft, Box, FolderOpen, RotateCcw } from 'lucide-vue-next'
import { computed, nextTick, onBeforeUnmount, onMounted, ref } from 'vue'
import { useRoute, useRouter } from 'vue-router'
import { ElMessage } from 'element-plus'
import FormPanel from '../components/FormPanel.vue'
import StepHeader from '../components/StepHeader.vue'
import { fetchScans, openOrderFolder, pointCloudFileUrl } from '../api/client'
import type { ScanResult } from '../types'

const route = useRoute()
const router = useRouter()
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
/** 容器尺寸变化监听器。 */
let resizeObserver: ResizeObserver | null = null

/** 当前 PLY 文件读取 URL。 */
const fileUrl = computed(() => (plyPath.value ? pointCloudFileUrl(plyPath.value) : ''))
/** 标题右侧状态文本。 */
const titleText = computed(() => (pointCount.value ? `${pointCount.value} 点` : '等待加载'))

/** 解析当前订单点云路径并初始化 vtk.js 加载流程。 */
onMounted(async () => {
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
  resizeObserver?.disconnect()
  resizeObserver = null
  genericWindow?.delete()
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
    const mapper = vtkMapper.newInstance()
    mapper.setInputConnection(reader.getOutputPort())
    mapper.setScalarVisibility(true)
    mapper.setColorModeToDirectScalars()
    mapper.setScalarModeToUsePointData()

    const actor = vtkActor.newInstance()
    actor.setMapper(mapper)
    actor.getProperty().setRepresentationToPoints()
    actor.getProperty().setPointSize(3)
    renderer.addActor(actor)
    renderer.resetCamera()
    renderWindow.render()

    const data = reader.getOutputData(0)
    pointCount.value = data?.getPoints?.().getNumberOfPoints?.() || 0
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
  renderer?.resetCamera()
  renderWindow?.render()
}

/** 请求后端打开当前订单目录。 */
async function openFolder() {
  if (!orderId.value) return
  const result = await openOrderFolder(orderId.value)
  if (result.ok) {
    ElMessage.success(`已打开目录：${result.path}`)
  }
}
</script>

<template>
  <main class="workflow-page pointcloud-page">
    <StepHeader active="process" />
    <section class="pointcloud-workspace">
      <FormPanel>
        <header class="form-heading">
          <div>
            <span class="eyebrow">三维处理</span>
            <h1>彩色点云显示</h1>
          </div>
          <span class="status-pill ready">{{ titleText }}</span>
        </header>
        <div class="pointcloud-toolbar">
          <button class="soft-btn" type="button" @click="router.push(`/send/${patientId}?orderId=${orderId}`)">
            <ArrowLeft :size="17" />返回交付
          </button>
          <button class="soft-btn" type="button" :disabled="loading || !!error" @click="resetCamera">
            <RotateCcw :size="17" />重置视角
          </button>
          <button class="soft-btn" type="button" :disabled="!orderId" @click="openFolder">
            <FolderOpen :size="17" />打开目录
          </button>
        </div>
        <section class="pointcloud-meta">
          <span><Box :size="16" />PLY</span>
          <strong>{{ plyPath || '未生成' }}</strong>
        </section>
      </FormPanel>
      <section class="pointcloud-stage">
        <div ref="viewerEl" class="pointcloud-viewer" />
        <div v-if="loading" class="pointcloud-overlay">点云加载中</div>
        <div v-else-if="error" class="pointcloud-overlay error">{{ error }}</div>
      </section>
    </section>
  </main>
</template>
