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
const patientId = computed(() => Number(route.params.id))
const orderId = computed(() => Number(route.query.orderId || 0))
const routePlyPath = computed(() => String(route.query.plyPath || ''))
const scans = ref<ScanResult[]>([])
const plyPath = ref('')
const pointCount = ref(0)
const loading = ref(true)
const error = ref('')
const viewerEl = ref<HTMLElement | null>(null)

let genericWindow: any = null
let renderer: any = null
let renderWindow: any = null
let resizeObserver: ResizeObserver | null = null

const fileUrl = computed(() => (plyPath.value ? pointCloudFileUrl(plyPath.value) : ''))
const titleText = computed(() => (pointCount.value ? `${pointCount.value} 点` : '等待加载'))

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

onBeforeUnmount(() => {
  resizeObserver?.disconnect()
  resizeObserver = null
  genericWindow?.delete()
})

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

function resetCamera() {
  renderer?.resetCamera()
  renderWindow?.render()
}

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
