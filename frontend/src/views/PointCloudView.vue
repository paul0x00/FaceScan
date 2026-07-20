<script setup lang="ts">
import '@kitware/vtk.js/Rendering/Profiles/Geometry'

import vtkCellArray from '@kitware/vtk.js/Common/Core/CellArray'
import vtkDataArray from '@kitware/vtk.js/Common/Core/DataArray'
import vtkPoints from '@kitware/vtk.js/Common/Core/Points'
import vtkPolyData from '@kitware/vtk.js/Common/DataModel/PolyData'
import vtkActor from '@kitware/vtk.js/Rendering/Core/Actor'
import vtkGenericRenderWindow from '@kitware/vtk.js/Rendering/Misc/GenericRenderWindow'
import vtkMapper from '@kitware/vtk.js/Rendering/Core/Mapper'
import vtkPLYReader from '@kitware/vtk.js/IO/Geometry/PLYReader'
import vtkPointPicker from '@kitware/vtk.js/Rendering/Core/PointPicker'
import { ElMessage } from 'element-plus'
import { Box, Circle, Grid3X3, Image, LassoSelect, MousePointer2, Pencil, Ruler, RotateCcw, Save, Trash2, Undo2, X } from 'lucide-vue-next'
import { computed, nextTick, onBeforeUnmount, onMounted, ref } from 'vue'
import { onBeforeRouteLeave, useRoute } from 'vue-router'
import StepHeader from '../components/StepHeader.vue'
import { fetchScans, pointCloudFileUrl, savePointCloudEdit } from '../api/client'
import type { ScanResult } from '../types'

interface ScreenPoint {
  x: number
  y: number
}

type EditMode = 'navigate' | 'lasso' | 'brush'
type MeasureType = 'distance' | 'angle' | 'symmetry'
type StandardView = 'back' | 'down' | 'front' | 'left' | 'right' | 'up'

interface WorldPoint {
  x: number
  y: number
  z: number
}

interface MeasurementRecord {
  id: number
  type: MeasureType
  points: WorldPoint[]
  value: number
  leftValue?: number
  rightValue?: number
  differencePercent?: number
}

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
/** 当前显示点数量。 */
const pointCount = ref(0)
/** 原始 PLY 点数量。 */
const originalPointCount = ref(0)
/** 点云加载状态。 */
const loading = ref(true)
/** 点云保存状态。 */
const saving = ref(false)
/** 点云加载或保存错误信息。 */
const error = ref('')
/** vtk.js 渲染容器。 */
const viewerEl = ref<HTMLElement | null>(null)
/** SVG 编辑覆盖层。 */
const editOverlayEl = ref<SVGSVGElement | null>(null)
/** 六视图弹窗根节点。 */
const viewPanelRef = ref<HTMLElement | null>(null)
/** 点云编辑弹窗根节点。 */
const editPanelRef = ref<HTMLElement | null>(null)
/** 测量弹窗根节点。 */
const measurePanelRef = ref<HTMLElement | null>(null)
/** 当前编辑工具。 */
const editMode = ref<EditMode>('navigate')
/** 圆形擦除半径，单位为 CSS 像素。 */
const brushRadius = ref(54)
/** 圆形擦除光标位置。 */
const brushPosition = ref<ScreenPoint | null>(null)
/** 当前正在绘制的套索折线。 */
const lassoPoints = ref<ScreenPoint[]>([])
/** 是否正在执行指针编辑。 */
const editingPointer = ref(false)
/** 未保存删除点数量。 */
const deletedPointCount = ref(0)
/** 撤销栈深度。 */
const undoDepth = ref(0)
/** 点云颜色显示开关。 */
const textureVisible = ref(true)
/** 六视图浮层开关。 */
const viewPanelOpen = ref(false)
/** 点云编辑选项浮层开关。 */
const editPanelOpen = ref(false)
/** 测量选项浮层开关。 */
const measurePanelOpen = ref(false)
/** 当前测量类型。 */
const measureType = ref<MeasureType>('distance')
/** 当前正在拾取的测量点。 */
const measurementPoints = ref<WorldPoint[]>([])
/** 已完成的测量记录。 */
const measurements = ref<MeasurementRecord[]>([])
/** 用于触发测量标注随相机刷新投影。 */
const measurementProjectionVersion = ref(0)
/** 当前选中的标准视角。 */
const activeView = ref<StandardView>('front')
/** 下一个测量记录编号。 */
let nextMeasurementId = 1

/** vtk.js 通用渲染窗口实例。 */
let genericWindow: any = null
/** vtk.js 渲染器实例。 */
let renderer: any = null
/** vtk.js 渲染窗口实例。 */
let renderWindow: any = null
/** vtk.js 点拾取器。 */
let pointPicker: any = null
/** vtk.js 渲染事件订阅。 */
let renderSubscription: { unsubscribe: () => void } | null = null
/** vtk.js mapper。 */
let mapper: any = null
/** vtk.js actor。 */
let actor: any = null
/** 当前点云包围盒。 */
let pointBounds: number[] | null = null
/** 容器尺寸变化监听器。 */
let resizeObserver: ResizeObserver | null = null
/** 原始点坐标，索引始终与 PLY 顶点顺序一致。 */
let originalCoordinates = new Float32Array()
/** 原始点颜色。 */
let originalColors: Uint8Array | null = null
/** 原始颜色分量数量。 */
let colorComponents = 0
/** 原始颜色数组名称。 */
let colorArrayName = 'RGB'
/** 原始点删除标记。 */
let deletedFlags = new Uint8Array()
/** 每次编辑操作删除的原始点索引。 */
let undoStack: number[][] = []
/** 当前圆形擦除笔画累计删除的原始点索引。 */
let brushStrokeDeleted: number[] = []
/** 圆形连续擦除的动画帧。 */
let brushFrame = 0
/** 上一次已经应用擦除的圆形中心。 */
let brushAppliedPosition: ScreenPoint | null = null
/** 当前笔画结束后是否需要自动回到浏览模式。 */
let browseAfterEditing = false
/** 正在进行的保存请求，防止连续导航触发重复覆盖。 */
let savePromise: Promise<boolean> | null = null
/** 路由离开前是否已经成功保存。 */
let savedBeforeLeave = false

/** 当前 PLY 文件读取 URL，附加时间戳避免重新进入时读取缓存。 */
const fileUrl = computed(() => (plyPath.value ? `${pointCloudFileUrl(plyPath.value)}&v=${Date.now()}` : ''))
/** 左上角状态文本。 */
const titleText = computed(() => (pointCount.value ? `${pointCount.value} 点` : '等待加载'))
/** 是否存在未保存编辑。 */
const dirty = computed(() => deletedPointCount.value > 0)
/** 套索 SVG points 属性。 */
const lassoPointText = computed(() => lassoPoints.value.map((point) => `${point.x},${point.y}`).join(' '))
/** 右侧编辑工具按钮显示文本。 */
const editToolLabel = computed(() => {
  if (editMode.value === 'lasso') return '圈选编辑中'
  if (editMode.value === 'brush') return '圆形编辑中'
  return '点云编辑'
})
/** 当前交互提示。 */
const interactionHint = computed(() => {
  if (measurePanelOpen.value) return measurementHint.value
  if (editMode.value === 'lasso') return '按住拖动绘制闭合区域，松开后删除区域内点云'
  if (editMode.value === 'brush') return '按住拖动圆形范围连续擦除，可在右侧调整半径'
  return '拖拽旋转 · 滚轮缩放 · Shift 平移'
})
/** 当前测量类型需要拾取的点数。 */
const measurementPointLimit = computed(() => (measureType.value === 'distance' ? 2 : measureType.value === 'angle' ? 3 : 4))
/** 当前测量模式的说明。 */
const measurementHint = computed(() => {
  const selected = `${measurementPoints.value.length}/${measurementPointLimit.value}`
  if (measureType.value === 'distance') return `距离测量：请选择起点和终点（${selected}）`
  if (measureType.value === 'angle') return `角度测量：依次选择第一边端点、顶点、第二边端点（${selected}）`
  return `左右差异：依次选择左侧两点、右侧两点（${selected}）`
})
/** 测量类型标题。 */
const measureTypeLabel = (type: MeasureType) => ({ distance: '距离', angle: '角度', symmetry: '左右差异' })[type]
/** 已完成的测量结果摘要。 */
const measurementSummary = (measurement: MeasurementRecord) => {
  if (measurement.type === 'distance') return formatMeasureValue(measurement.value)
  if (measurement.type === 'angle') return `${measurement.value.toFixed(1)}°`
  return `差 ${formatMeasureValue(measurement.value)} / ${measurement.differencePercent?.toFixed(1) || '0.0'}%`
}
/** 测量数值格式化。 */
function formatMeasureValue(value: number) {
  if (value >= 100) return `${value.toFixed(1)} 坐标单位`
  if (value >= 10) return `${value.toFixed(2)} 坐标单位`
  return `${value.toFixed(3)} 坐标单位`
}
/** 六视图列表。 */
const viewOptions = [
  { key: 'back', label: '后视图' },
  { key: 'down', label: '下视图' },
  { key: 'front', label: '前视图' },
  { key: 'left', label: '左视图' },
  { key: 'right', label: '右视图' },
  { key: 'up', label: '上视图' }
] as const

const measurementSegments = (type: MeasureType, pointCount: number) => {
  if (type === 'distance') return pointCount >= 2 ? [[0, 1]] : []
  if (type === 'angle') return pointCount >= 2 ? [[0, 1], ...(pointCount >= 3 ? [[1, 2]] : [])] : []
  return pointCount >= 2 ? [[0, 1], ...(pointCount >= 4 ? [[2, 3]] : [])] : []
}

/** 解析当前订单点云路径并初始化 vtk.js 加载流程。 */
onMounted(async () => {
  document.addEventListener('pointerdown', handleDocumentPointerDown)
  window.addEventListener('beforeunload', handleBeforeUnload)
  window.addEventListener('pagehide', handlePageHide)
  try {
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
  } catch (err) {
    error.value = err instanceof Error ? err.message : '点云加载失败'
    loading.value = false
  }
})

/** 离开当前路由前保存点云；保存失败时取消跳转。 */
onBeforeRouteLeave(async () => {
  if (!dirty.value || savedBeforeLeave) return true
  return saveEdits()
})

/** 销毁 vtk.js 资源和全局监听器。 */
onBeforeUnmount(() => {
  document.removeEventListener('pointerdown', handleDocumentPointerDown)
  window.removeEventListener('beforeunload', handleBeforeUnload)
  window.removeEventListener('pagehide', handlePageHide)
  if (brushFrame) cancelAnimationFrame(brushFrame)
  brushFrame = 0
  renderSubscription?.unsubscribe()
  renderSubscription = null
  resizeObserver?.disconnect()
  resizeObserver = null
  pointPicker?.delete?.()
  pointPicker = null
  genericWindow?.delete()
  mapper = null
  actor = null
  pointBounds = null
})

/** 加载 PLY 点云并创建可编辑的 vtk.js 点渲染管线。 */
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
    renderSubscription = genericWindow.getInteractor().onRenderEvent(() => {
      measurementProjectionVersion.value += 1
    })

    const reader = vtkPLYReader.newInstance()
    await reader.setUrl(fileUrl.value, { binary: false })
    const data = reader.getOutputData(0)
    const sourcePoints = data?.getPoints?.()
    const sourceCoordinates = sourcePoints?.getData?.()
    if (!sourceCoordinates?.length) throw new Error('PLY 文件中没有可编辑点数据')

    originalCoordinates = new Float32Array(sourceCoordinates.length)
    originalCoordinates.set(sourceCoordinates)
    originalPointCount.value = sourcePoints.getNumberOfPoints()
    pointCount.value = originalPointCount.value
    deletedFlags = new Uint8Array(originalPointCount.value)

    const sourceScalars = data?.getPointData?.().getScalars?.()
    const scalarData = sourceScalars?.getData?.()
    colorComponents = sourceScalars?.getNumberOfComponents?.() || 0
    colorArrayName = sourceScalars?.getName?.() || 'RGB'
    if (scalarData?.length && colorComponents > 0) {
      originalColors = new Uint8Array(scalarData.length)
      originalColors.set(scalarData)
    }

    mapper = vtkMapper.newInstance()
    mapper.setScalarVisibility(true)
    mapper.setColorModeToDirectScalars()
    mapper.setScalarModeToUsePointData()
    actor = vtkActor.newInstance()
    actor.setMapper(mapper)
    pointPicker = vtkPointPicker.newInstance({ tolerance: 0.012 })
    pointPicker.setPickFromList(true)
    pointPicker.addPickList(actor)
    actor.getProperty().setRepresentationToPoints()
    actor.getProperty().setPointSize(3)
    renderer.addActor(actor)
    rebuildDisplayedPointCloud()
    renderer.resetCamera()
    renderWindow.render()
    pointBounds = actor.getBounds?.() || data?.getBounds?.() || null

    resizeObserver = new ResizeObserver(() => {
      genericWindow?.resize()
      renderWindow?.render()
    })
    resizeObserver.observe(viewerEl.value)
  } finally {
    loading.value = false
  }
}

/** 根据删除标记重建当前显示的 vtkPolyData。 */
function rebuildDisplayedPointCloud() {
  if (!mapper) return
  const remainingCount = originalPointCount.value - deletedPointCount.value
  const coordinates = new Float32Array(remainingCount * 3)
  const verts = new Uint32Array(remainingCount * 2)
  const colors = originalColors && colorComponents > 0 ? new Uint8Array(remainingCount * colorComponents) : null
  let targetIndex = 0
  for (let sourceIndex = 0; sourceIndex < originalPointCount.value; sourceIndex += 1) {
    if (deletedFlags[sourceIndex]) continue
    const sourceOffset = sourceIndex * 3
    const targetOffset = targetIndex * 3
    coordinates[targetOffset] = originalCoordinates[sourceOffset]
    coordinates[targetOffset + 1] = originalCoordinates[sourceOffset + 1]
    coordinates[targetOffset + 2] = originalCoordinates[sourceOffset + 2]
    verts[targetIndex * 2] = 1
    verts[targetIndex * 2 + 1] = targetIndex
    if (colors && originalColors) {
      const sourceColorOffset = sourceIndex * colorComponents
      const targetColorOffset = targetIndex * colorComponents
      for (let component = 0; component < colorComponents; component += 1) {
        colors[targetColorOffset + component] = originalColors[sourceColorOffset + component]
      }
    }
    targetIndex += 1
  }

  const points = vtkPoints.newInstance()
  points.setData(coordinates, 3)
  const polyData = vtkPolyData.newInstance()
  polyData.setPoints(points)
  polyData.setVerts(vtkCellArray.newInstance({ values: verts }))
  if (colors) {
    polyData.getPointData().setScalars(vtkDataArray.newInstance({
      name: colorArrayName,
      numberOfComponents: colorComponents,
      values: colors
    }))
  }
  polyData.modified()
  mapper.setInputData(polyData)
  mapper.modified()
  mapper.update?.()
  actor?.modified?.()
  pointCount.value = remainingCount
  renderWindow?.render()
}

/** 切换编辑模式；编辑覆盖层负责阻止相机接收编辑指针事件。 */
function setEditMode(mode: EditMode) {
  editMode.value = mode
  viewPanelOpen.value = false
  measurePanelOpen.value = false
  lassoPoints.value = []
  brushPosition.value = null
  measurementPoints.value = []
  if (mode === 'navigate') browseAfterEditing = false
}

/** 将点云世界坐标投影为覆盖层坐标。 */
function projectWorldPoint(point: WorldPoint): ScreenPoint | null {
  const rect = viewerEl.value?.getBoundingClientRect()
  const apiWindow = genericWindow?.getApiSpecificRenderWindow?.()
  const renderSize = apiWindow?.getSize?.()
  if (!rect || !apiWindow || !renderSize?.[0] || !renderSize?.[1]) return null
  const display = apiWindow.worldToDisplay(point.x, point.y, point.z, renderer)
  if (!display || display[2] < 0 || display[2] > 1) return null
  return {
    x: display[0] * rect.width / renderSize[0],
    y: rect.height - display[1] * rect.height / renderSize[1]
  }
}

const measurementGraphics = computed(() => {
  measurementProjectionVersion.value
  return measurements.value.map((measurement) => ({
    ...measurement,
    screenPoints: measurement.points.map(projectWorldPoint),
    segments: measurementSegments(measurement.type, measurement.points.length)
      .map(([start, end]) => ({ start: projectWorldPoint(measurement.points[start]), end: projectWorldPoint(measurement.points[end]) }))
      .filter((segment): segment is { start: ScreenPoint; end: ScreenPoint } => !!segment.start && !!segment.end),
    labelPoint: projectWorldPoint(measurementLabelPoint(measurement))
  }))
})

const activeMeasurementGraphics = computed(() => {
  measurementProjectionVersion.value
  return {
    screenPoints: measurementPoints.value.map(projectWorldPoint),
    segments: measurementSegments(measureType.value, measurementPoints.value.length)
      .map(([start, end]) => ({ start: projectWorldPoint(measurementPoints.value[start]), end: projectWorldPoint(measurementPoints.value[end]) }))
      .filter((segment): segment is { start: ScreenPoint; end: ScreenPoint } => !!segment.start && !!segment.end)
  }
})

function measurementLabelPoint(measurement: MeasurementRecord): WorldPoint {
  if (measurement.type === 'angle') return measurement.points[1]
  const points = measurement.type === 'symmetry' ? measurement.points : measurement.points.slice(0, 2)
  return points.reduce((center, point) => ({
    x: center.x + point.x / points.length,
    y: center.y + point.y / points.length,
    z: center.z + point.z / points.length
  }), { x: 0, y: 0, z: 0 })
}

function vectorLength(point: WorldPoint) {
  return Math.hypot(point.x, point.y, point.z)
}

function distanceBetween(first: WorldPoint, second: WorldPoint) {
  return Math.hypot(first.x - second.x, first.y - second.y, first.z - second.z)
}

function angleBetween(first: WorldPoint, vertex: WorldPoint, second: WorldPoint) {
  const a = { x: first.x - vertex.x, y: first.y - vertex.y, z: first.z - vertex.z }
  const b = { x: second.x - vertex.x, y: second.y - vertex.y, z: second.z - vertex.z }
  const denominator = vectorLength(a) * vectorLength(b)
  if (!denominator) return 0
  const cosine = Math.max(-1, Math.min(1, (a.x * b.x + a.y * b.y + a.z * b.z) / denominator))
  return Math.acos(cosine) * 180 / Math.PI
}

function createMeasurement(points: WorldPoint[]): MeasurementRecord {
  if (measureType.value === 'distance') {
    return { id: nextMeasurementId++, type: 'distance', points, value: distanceBetween(points[0], points[1]) }
  }
  if (measureType.value === 'angle') {
    return { id: nextMeasurementId++, type: 'angle', points, value: angleBetween(points[0], points[1], points[2]) }
  }
  const leftValue = distanceBetween(points[0], points[1])
  const rightValue = distanceBetween(points[2], points[3])
  const value = Math.abs(leftValue - rightValue)
  return {
    id: nextMeasurementId++,
    type: 'symmetry',
    points,
    value,
    leftValue,
    rightValue,
    differencePercent: value / Math.max(leftValue, rightValue, Number.EPSILON) * 100
  }
}

/** 使用 vtk.js 在点云上拾取一个三维点。 */
function pickMeasurementPoint(event: PointerEvent) {
  const rect = viewerEl.value?.getBoundingClientRect()
  const apiWindow = genericWindow?.getApiSpecificRenderWindow?.()
  const renderSize = apiWindow?.getSize?.()
  if (!rect || !apiWindow || !renderSize?.[0] || !renderSize?.[1] || !renderer || !actor) return
  const local = localPointer(event)
  if (!local) return
  const displayX = local.x * renderSize[0] / rect.width
  const displayY = (rect.height - local.y) * renderSize[1] / rect.height
  pointPicker?.pick([displayX, displayY, 0], renderer)
  const pickedPointId = pointPicker?.getPointId?.() ?? -1
  if (!pointPicker || !pointPicker.getActors?.().includes(actor) || pickedPointId < 0) {
    exitMeasurementFromPointer(event)
    return
  }
  const position: number[] = []
  mapper?.getInputData?.()?.getPoints?.()?.getPoint?.(pickedPointId, position)
  if (position.length < 3) {
    exitMeasurementFromPointer(event)
    return
  }
  const point = { x: position[0], y: position[1], z: position[2] }
  const projectedPoint = projectWorldPoint(point)
  if (!projectedPoint || Math.hypot(projectedPoint.x - local.x, projectedPoint.y - local.y) > 12) {
    exitMeasurementFromPointer(event)
    return
  }
  if (measurementPoints.value.some((item) => distanceBetween(item, point) < 1e-6)) {
    ElMessage.info('该点已被选中，请选择其他点')
    return
  }
  event.preventDefault()
  event.stopPropagation()
  measurementPoints.value = [...measurementPoints.value, point]
  if (measurementPoints.value.length === measurementPointLimit.value) {
    measurements.value = [...measurements.value, createMeasurement(measurementPoints.value)]
    measurementPoints.value = []
    ElMessage.success(`${measureTypeLabel(measureType.value)}测量已完成`)
  }
}

/** 切换测量类型并进入拾取状态。 */
function setMeasurementType(type: MeasureType) {
  measureType.value = type
  measurementPoints.value = []
  measurePanelOpen.value = true
  editPanelOpen.value = false
  viewPanelOpen.value = false
  setEditMode('navigate')
  measurePanelOpen.value = true
}

function closeMeasurement() {
  measurePanelOpen.value = false
  measurementPoints.value = []
}

function exitMeasurementFromPointer(event: PointerEvent) {
  event.preventDefault()
  event.stopPropagation()
  closeMeasurement()
}

function toggleMeasurePanel() {
  if (measurePanelOpen.value) {
    closeMeasurement()
    return
  }
  setMeasurementType(measureType.value)
}

function undoMeasurementPoint() {
  if (measurementPoints.value.length) measurementPoints.value = measurementPoints.value.slice(0, -1)
}

function removeMeasurement(id: number) {
  measurements.value = measurements.value.filter((item) => item.id !== id)
}

function clearMeasurements() {
  measurements.value = []
  measurementPoints.value = []
}

/** 将 DOM 指针坐标转换为覆盖层局部坐标。 */
function localPointer(event: PointerEvent): ScreenPoint | null {
  const rect = editOverlayEl.value?.getBoundingClientRect()
  if (!rect) return null
  return { x: event.clientX - rect.left, y: event.clientY - rect.top }
}

/** 编辑覆盖层按下事件。 */
function handleEditorPointerDown(event: PointerEvent) {
  if (loading.value || !!error.value) return
  if (measurePanelOpen.value) {
    pickMeasurementPoint(event)
    return
  }
  if (editMode.value === 'navigate') return
  const point = localPointer(event)
  if (!point) return
  event.preventDefault()
  editOverlayEl.value?.setPointerCapture(event.pointerId)
  editingPointer.value = true
  if (editMode.value === 'lasso') {
    lassoPoints.value = [point]
  } else {
    brushStrokeDeleted = []
    brushPosition.value = point
    brushAppliedPosition = point
    eraseWithBrush(point)
  }
}

/** 编辑覆盖层移动事件。 */
function handleEditorPointerMove(event: PointerEvent) {
  const point = localPointer(event)
  if (!point) return
  if (editMode.value === 'brush') brushPosition.value = point
  if (!editingPointer.value) return
  event.preventDefault()
  if (editMode.value === 'lasso') {
    const previous = lassoPoints.value[lassoPoints.value.length - 1]
    if (!previous || Math.hypot(point.x - previous.x, point.y - previous.y) >= 3) {
      lassoPoints.value = [...lassoPoints.value, point]
    }
  } else if (!brushFrame) {
    brushFrame = requestAnimationFrame(() => {
      brushFrame = 0
      applyPendingBrushSweep()
    })
  }
}

/** 编辑覆盖层抬起事件。 */
function handleEditorPointerUp(event: PointerEvent) {
  if (!editingPointer.value) return
  editingPointer.value = false
  if (editOverlayEl.value?.hasPointerCapture(event.pointerId)) {
    editOverlayEl.value.releasePointerCapture(event.pointerId)
  }
  if (editMode.value === 'lasso') {
    const polygon = lassoPoints.value
    lassoPoints.value = []
    if (polygon.length >= 3) deleteProjectedPoints((point) => isPointInPolygon(point, polygon), true)
  } else {
    if (brushFrame) {
      cancelAnimationFrame(brushFrame)
      brushFrame = 0
    }
    applyPendingBrushSweep()
    brushAppliedPosition = null
    if (brushStrokeDeleted.length) {
      undoStack.push(brushStrokeDeleted)
      undoDepth.value = undoStack.length
      brushStrokeDeleted = []
    }
  }
  if (browseAfterEditing) setEditMode('navigate')
}

/** 指针取消时结束当前编辑笔画。 */
function handleEditorPointerCancel(event: PointerEvent) {
  if (editingPointer.value) handleEditorPointerUp(event)
}

/** 应用尚未处理的圆形拖动轨迹。 */
function applyPendingBrushSweep() {
  if (!brushPosition.value || !brushAppliedPosition) return
  eraseWithBrushSweep(brushAppliedPosition, brushPosition.value)
  brushAppliedPosition = { ...brushPosition.value }
}

/** 使用圆形从起点扫掠到终点，避免快速拖动时轨迹出现空隙。 */
function eraseWithBrushSweep(start: ScreenPoint, end: ScreenPoint) {
  const segmentX = end.x - start.x
  const segmentY = end.y - start.y
  const segmentLengthSquared = segmentX * segmentX + segmentY * segmentY
  const radiusSquared = brushRadius.value * brushRadius.value
  deleteProjectedPoints((point) => {
    let ratio = 0
    if (segmentLengthSquared > 0) {
      ratio = Math.max(0, Math.min(1,
        ((point.x - start.x) * segmentX + (point.y - start.y) * segmentY) / segmentLengthSquared))
    }
    const nearestX = start.x + ratio * segmentX
    const nearestY = start.y + ratio * segmentY
    const dx = point.x - nearestX
    const dy = point.y - nearestY
    return dx * dx + dy * dy <= radiusSquared
  }, false)
}

/** 使用当前圆形范围擦除点云。 */
function eraseWithBrush(center: ScreenPoint) {
  eraseWithBrushSweep(center, center)
}

/** 将符合当前屏幕区域判断的点标记为删除。 */
function deleteProjectedPoints(contains: (point: ScreenPoint) => boolean, recordHistory: boolean) {
  const rect = viewerEl.value?.getBoundingClientRect()
  const apiWindow = genericWindow?.getApiSpecificRenderWindow?.()
  const renderSize = apiWindow?.getSize?.()
  if (!rect || !apiWindow || !renderSize?.[0] || !renderSize?.[1]) return

  const removed: number[] = []
  for (let index = 0; index < originalPointCount.value; index += 1) {
    if (deletedFlags[index]) continue
    const offset = index * 3
    const display = apiWindow.worldToDisplay(
      originalCoordinates[offset],
      originalCoordinates[offset + 1],
      originalCoordinates[offset + 2],
      renderer
    )
    if (!display || display[2] < 0 || display[2] > 1) continue
    const screenPoint = {
      x: display[0] * rect.width / renderSize[0],
      y: rect.height - display[1] * rect.height / renderSize[1]
    }
    if (contains(screenPoint)) removed.push(index)
  }
  if (!removed.length) return
  if (removed.length >= pointCount.value) {
    ElMessage.warning('点云至少需要保留一个点，请缩小编辑范围')
    return
  }
  for (const index of removed) deletedFlags[index] = 1
  savedBeforeLeave = false
  deletedPointCount.value += removed.length
  if (recordHistory) {
    undoStack.push(removed)
    undoDepth.value = undoStack.length
  } else {
    brushStrokeDeleted.push(...removed)
  }
  rebuildDisplayedPointCloud()
}

/** 判断屏幕点是否位于套索多边形内。 */
function isPointInPolygon(point: ScreenPoint, polygon: ScreenPoint[]) {
  let inside = false
  for (let i = 0, j = polygon.length - 1; i < polygon.length; j = i, i += 1) {
    const a = polygon[i]
    const b = polygon[j]
    const intersects = ((a.y > point.y) !== (b.y > point.y))
      && point.x < (b.x - a.x) * (point.y - a.y) / ((b.y - a.y) || Number.EPSILON) + a.x
    if (intersects) inside = !inside
  }
  return inside
}

/** 撤销最近一次圈选或圆形擦除笔画。 */
function undoEdit() {
  const removed = undoStack.pop()
  if (!removed) return
  for (const index of removed) deletedFlags[index] = 0
  deletedPointCount.value = Math.max(0, deletedPointCount.value - removed.length)
  undoDepth.value = undoStack.length
  rebuildDisplayedPointCloud()
}

/** 放弃当前页面内的全部编辑。 */
function resetEdits() {
  if (!dirty.value) return
  deletedFlags.fill(0)
  deletedPointCount.value = 0
  undoStack = []
  undoDepth.value = 0
  rebuildDisplayedPointCloud()
}

/** 将删除索引压缩为 start/count 对，降低保存请求体积。 */
function buildDeletedRanges() {
  const ranges: number[] = []
  let start = -1
  let count = 0
  for (let index = 0; index <= deletedFlags.length; index += 1) {
    if (index < deletedFlags.length && deletedFlags[index]) {
      if (start < 0) start = index
      count += 1
    } else if (start >= 0) {
      ranges.push(start, count)
      start = -1
      count = 0
    }
  }
  return ranges
}

/** 将已成功写盘的删除结果提交为新的页面基线。 */
function commitSavedEdits() {
  const remainingCount = pointCount.value
  const coordinates = new Float32Array(remainingCount * 3)
  const colors = originalColors && colorComponents > 0 ? new Uint8Array(remainingCount * colorComponents) : null
  let targetIndex = 0
  for (let sourceIndex = 0; sourceIndex < originalPointCount.value; sourceIndex += 1) {
    if (deletedFlags[sourceIndex]) continue
    coordinates.set(originalCoordinates.subarray(sourceIndex * 3, sourceIndex * 3 + 3), targetIndex * 3)
    if (colors && originalColors) {
      colors.set(
        originalColors.subarray(sourceIndex * colorComponents, sourceIndex * colorComponents + colorComponents),
        targetIndex * colorComponents
      )
    }
    targetIndex += 1
  }
  originalCoordinates = coordinates
  originalColors = colors
  originalPointCount.value = remainingCount
  deletedFlags = new Uint8Array(remainingCount)
  deletedPointCount.value = 0
  undoStack = []
  undoDepth.value = 0
  brushStrokeDeleted = []
}

/** 保存编辑结果并返回是否允许离开页面。 */
function saveEdits(): Promise<boolean> {
  if (!dirty.value || !plyPath.value) return Promise.resolve(true)
  if (savePromise) return savePromise
  saving.value = true
  savePromise = (async () => {
    try {
      const result = await savePointCloudEdit(plyPath.value, originalPointCount.value, buildDeletedRanges())
      commitSavedEdits()
      savedBeforeLeave = true
      ElMessage.success(`点云已更新，本地文件保留 ${result.pointCount} 个点`)
      return true
    } catch (err: any) {
      ElMessage.error(err?.response?.data?.error || err?.message || '点云文件保存失败，请重试')
      return false
    } finally {
      saving.value = false
      savePromise = null
    }
  })()
  return savePromise
}

/** 浏览器刷新或关闭前提示仍有未保存编辑。 */
function handleBeforeUnload(event: BeforeUnloadEvent) {
  if (!dirty.value || savedBeforeLeave) return
  event.preventDefault()
  event.returnValue = ''
}

/** 页面被浏览器回收时使用 Beacon 尽力提交编辑结果。 */
function handlePageHide() {
  if (!dirty.value || savedBeforeLeave || !plyPath.value) return
  const body = JSON.stringify({ path: plyPath.value, expectedPointCount: originalPointCount.value, deletedRanges: buildDeletedRanges() })
  navigator.sendBeacon('/api/pointcloud/edit', new Blob([body], { type: 'application/json' }))
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
  if (textureVisible.value && originalColors) {
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
  editPanelOpen.value = false
  measurePanelOpen.value = false
  measurementPoints.value = []
}

/** 展开或收起点云编辑选项；主动收起时同步回到浏览模式。 */
function toggleEditPanel() {
  if (editPanelOpen.value) {
    editPanelOpen.value = false
    setEditMode('navigate')
    return
  }
  editPanelOpen.value = true
  viewPanelOpen.value = false
  measurePanelOpen.value = false
  measurementPoints.value = []
}

/** 点击工具弹窗外部后关闭面板，并在当前编辑笔画结束后回到浏览模式。 */
function handleDocumentPointerDown(event: PointerEvent) {
  const target = event.target as HTMLElement | null
  if (!target) return
  if (viewPanelOpen.value
    && !target.closest('.pointcloud-toolbtn--views')
    && !viewPanelRef.value?.contains(target)) {
    viewPanelOpen.value = false
  }
  if (measurePanelOpen.value
    && !target.closest('.pointcloud-toolbtn--measure')
    && !measurePanelRef.value?.contains(target)
    && !editOverlayEl.value?.contains(target)) {
    closeMeasurement()
  }
  if (editPanelOpen.value
    && !target.closest('.pointcloud-toolbtn--edit')
    && !editPanelRef.value?.contains(target)) {
    editPanelOpen.value = false
    if (editingPointer.value && editMode.value !== 'navigate') {
      browseAfterEditing = true
    } else {
      setEditMode('navigate')
    }
  }
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
  } else {
    camera.setPosition(centerX, centerY - distance, centerZ)
    camera.setViewUp(0, 0, 1)
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
        <svg
          ref="editOverlayEl"
          class="pointcloud-edit-overlay"
          :class="[`mode-${editMode}`, { 'mode-measure': measurePanelOpen }]"
          @pointerdown="handleEditorPointerDown"
          @pointermove="handleEditorPointerMove"
          @pointerup="handleEditorPointerUp"
          @pointercancel="handleEditorPointerCancel"
          @pointerleave="editMode === 'brush' && !editingPointer ? brushPosition = null : undefined"
        >
          <polygon v-if="lassoPoints.length > 2" class="pointcloud-lasso-fill" :points="lassoPointText" />
          <polyline v-if="lassoPoints.length" class="pointcloud-lasso-line" :points="lassoPointText" />
          <circle
            v-if="editMode === 'brush' && brushPosition"
            class="pointcloud-brush-ring"
            :cx="brushPosition.x"
            :cy="brushPosition.y"
            :r="brushRadius"
          />

          <g v-for="measurement in measurementGraphics" :key="measurement.id" class="pointcloud-measurement">
            <line
              v-for="(segment, index) in measurement.segments"
              :key="`${measurement.id}-line-${index}`"
              class="pointcloud-measure-line"
              :class="`type-${measurement.type}`"
              :x1="segment.start.x"
              :y1="segment.start.y"
              :x2="segment.end.x"
              :y2="segment.end.y"
            />
            <g v-for="(point, index) in measurement.screenPoints" :key="`${measurement.id}-point-${index}`">
              <circle v-if="point" class="pointcloud-measure-point" :cx="point.x" :cy="point.y" r="5" />
            </g>
            <g v-if="measurement.labelPoint" class="pointcloud-measure-label">
              <rect
                :x="measurement.labelPoint.x - 90"
                :y="measurement.labelPoint.y - 31"
                width="180"
                height="24"
                rx="12"
              />
              <text :x="measurement.labelPoint.x" :y="measurement.labelPoint.y - 15">
                {{ measurementSummary(measurement) }}
              </text>
            </g>
          </g>

          <g v-if="measurePanelOpen" class="pointcloud-measurement active">
            <line
              v-for="(segment, index) in activeMeasurementGraphics.segments"
              :key="`active-line-${index}`"
              class="pointcloud-measure-line active"
              :x1="segment.start.x"
              :y1="segment.start.y"
              :x2="segment.end.x"
              :y2="segment.end.y"
            />
            <g v-for="(point, index) in activeMeasurementGraphics.screenPoints" :key="`active-point-${index}`">
              <g v-if="point">
                <circle class="pointcloud-measure-point active" :cx="point.x" :cy="point.y" r="7" />
                <text class="pointcloud-measure-index" :x="point.x" :y="point.y + 3">{{ index + 1 }}</text>
              </g>
            </g>
          </g>
        </svg>

        <div class="pointcloud-stage-pill">
          <span class="status-pill ready"><Box :size="14" />{{ titleText }} · 已删除 {{ deletedPointCount }} 点</span>
          <span v-if="dirty" class="pointcloud-unsaved"><Save :size="13" />离开页面时自动保存</span>
        </div>

        <div class="pointcloud-toolrail">
          <button class="pointcloud-toolbtn soft-btn" type="button" :disabled="loading || !!error || editMode !== 'navigate'" @click="resetCamera">
            <RotateCcw :size="17" /><span>重置视角</span>
          </button>
          <button class="pointcloud-toolbtn soft-btn" type="button" :class="{ active: textureVisible }" :disabled="loading || !!error" @click="toggleTexture">
            <Image :size="17" /><span>{{ textureVisible ? '纹理显示' : '纹理关闭' }}</span>
          </button>
          <button class="pointcloud-toolbtn pointcloud-toolbtn--views soft-btn" type="button" :class="{ active: viewPanelOpen }" :disabled="loading || !!error || editMode !== 'navigate'" @click="toggleViewPanel">
            <Grid3X3 :size="17" /><span>六视图</span>
          </button>
          <button
            class="pointcloud-toolbtn pointcloud-toolbtn--measure soft-btn"
            type="button"
            :class="{ active: measurePanelOpen }"
            :disabled="loading || !!error || editMode !== 'navigate'"
            @click="toggleMeasurePanel"
          >
            <Ruler :size="17" /><span>测量</span>
          </button>
          <button
            class="pointcloud-toolbtn pointcloud-toolbtn--edit soft-btn"
            type="button"
            :class="{ active: editPanelOpen || editMode !== 'navigate' }"
            :disabled="loading || !!error"
            @click="toggleEditPanel"
          >
            <Pencil :size="17" /><span>{{ editToolLabel }}</span>
          </button>
        </div>

        <div v-if="measurePanelOpen" ref="measurePanelRef" class="pointcloud-measurepanel">
          <div class="pointcloud-editpanel-head">
            <div>
              <strong>基础测量</strong>
              <span>在点云表面依次选择测量点，结果使用 PLY 坐标单位</span>
            </div>
            <span class="pointcloud-measure-count">{{ measurements.length }} 项</span>
          </div>
          <div class="pointcloud-measure-modes">
            <button class="pointcloud-editbtn" type="button" :class="{ active: measureType === 'distance' }" @click="setMeasurementType('distance')">距离</button>
            <button class="pointcloud-editbtn" type="button" :class="{ active: measureType === 'angle' }" @click="setMeasurementType('angle')">角度</button>
            <button class="pointcloud-editbtn" type="button" :class="{ active: measureType === 'symmetry' }" @click="setMeasurementType('symmetry')">左右差异</button>
          </div>
          <div class="pointcloud-measure-guide">
            <strong>{{ measurementHint }}</strong>
            <span v-if="measureType === 'symmetry'">左侧长度与右侧长度按四个选点的先后顺序比较</span>
            <span v-else-if="measureType === 'angle'">第 2 个点作为角度顶点</span>
            <span v-else>测量两个点之间的三维直线距离</span>
          </div>
          <div class="pointcloud-measure-actions">
            <button class="pointcloud-editbtn" type="button" :disabled="!measurementPoints.length" @click="undoMeasurementPoint">
              <Undo2 :size="16" />撤销选点
            </button>
            <button class="pointcloud-editbtn danger" type="button" :disabled="!measurements.length && !measurementPoints.length" @click="clearMeasurements">
              <Trash2 :size="16" />全部清除
            </button>
          </div>
          <div v-if="measurements.length" class="pointcloud-measure-results">
            <div v-for="measurement in measurements" :key="measurement.id" class="pointcloud-measure-result">
              <div>
                <strong>{{ measureTypeLabel(measurement.type) }} {{ measurement.id }}</strong>
                <span>{{ measurementSummary(measurement) }}</span>
                <small v-if="measurement.type === 'symmetry'">
                  左 {{ formatMeasureValue(measurement.leftValue || 0) }} · 右 {{ formatMeasureValue(measurement.rightValue || 0) }}
                </small>
              </div>
              <button type="button" title="删除该测量" aria-label="删除该测量" @click="removeMeasurement(measurement.id)"><X :size="15" /></button>
            </div>
          </div>
        </div>

        <div v-if="editPanelOpen" ref="editPanelRef" class="pointcloud-editpanel">
          <div class="pointcloud-editpanel-head">
            <div>
              <strong>点云编辑</strong>
              <span>编辑效果实时显示在三维模型中</span>
            </div>
            <span class="pointcloud-edit-count">已删除 {{ deletedPointCount }} 点</span>
          </div>
          <div class="pointcloud-edit-modes">
            <button class="pointcloud-editbtn" type="button" :class="{ active: editMode === 'navigate' }" @click="setEditMode('navigate')">
              <MousePointer2 :size="17" />浏览
            </button>
            <button class="pointcloud-editbtn" type="button" :class="{ active: editMode === 'lasso' }" @click="setEditMode('lasso')">
              <LassoSelect :size="17" />圈选去除
            </button>
            <button class="pointcloud-editbtn" type="button" :class="{ active: editMode === 'brush' }" @click="setEditMode('brush')">
              <Circle :size="17" />圆形去除
            </button>
          </div>
          <label v-if="editMode === 'brush'" class="pointcloud-radius-control">
            <span>圆形范围</span>
            <input v-model.number="brushRadius" type="range" min="16" max="160" step="2" />
            <strong>{{ brushRadius }} px</strong>
          </label>
          <div class="pointcloud-edit-actions">
            <button class="pointcloud-editbtn" type="button" :disabled="!undoDepth" @click="undoEdit">
              <Undo2 :size="17" />撤销上一步
            </button>
            <button class="pointcloud-editbtn danger" type="button" :disabled="!dirty" @click="resetEdits">
              <Trash2 :size="17" />清除编辑
            </button>
          </div>
        </div>

        <div v-if="viewPanelOpen" ref="viewPanelRef" class="pointcloud-viewpanel">
          <button
            v-for="view in viewOptions"
            :key="view.key"
            class="pointcloud-viewcell"
            :class="{ active: activeView === view.key }"
            type="button"
            :disabled="loading || !!error"
            :title="view.label"
            :aria-label="view.label"
            @click="applyView(view.key)"
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

        <div class="pointcloud-hintbar"><span>{{ interactionHint }}</span></div>
        <div v-if="loading || saving" class="pointcloud-overlay">{{ saving ? '正在更新本地点云文件' : '点云加载中' }}</div>
        <div v-else-if="error" class="pointcloud-overlay error">{{ error }}</div>
      </section>
    </section>
  </main>
</template>
