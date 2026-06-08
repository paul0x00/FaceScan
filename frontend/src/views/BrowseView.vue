<script setup lang="ts">
import { computed, onBeforeUnmount, onMounted, reactive, ref, watch } from 'vue'
import { useRouter } from 'vue-router'
import { CalendarDays, ChevronDown, CirclePlus, ClipboardList, Edit, FolderOpen, RotateCcw, Search, Stethoscope, Trash2, UserPlus, UserRound } from 'lucide-vue-next'
import { ElMessage, ElMessageBox } from 'element-plus'
import { createOrder, deleteOrder, deletePatient, fetchOrders, imageFileUrl, openOrderFolder } from '../api/client'
import { usePatientStore } from '../stores/patients'
import type { Order } from '../types'
import LogoMark from '../components/LogoMark.vue'
import WindowIcons from '../components/WindowIcons.vue'

const router = useRouter()
const store = usePatientStore()
/** 患者检索条件。 */
const filters = reactive({
  keyword: '',
  startDate: '',
  endDate: ''
})
/** 日期范围弹层是否展开。 */
const datePanelOpen = ref(false)
/** 日期范围控件根节点，用于判断外部点击。 */
const dateRangeRef = ref<HTMLElement>()
/** 日期范围临时选择，点击应用后才写入检索条件。 */
const dateDraft = reactive({
  startDate: '',
  endDate: '',
  preset: ''
})
/** 当前激活的患者主键。 */
const activeId = ref(0)
/** 搜索按钮加载状态。 */
const searching = ref(false)
/** 当前患者订单列表。 */
const orders = ref<Order[]>([])
/** 订单列表加载状态。 */
const ordersLoading = ref(false)
/** 缩略图加载失败的患者集合。 */
const brokenThumbnails = ref<Record<number, boolean>>({})

/** 浏览页日期快捷范围。 */
const datePresets = [
  { key: 'today', label: '今天' },
  { key: 'yesterday', label: '昨天' },
  { key: 'last24h', label: '近24小时' },
  { key: 'last7d', label: '近 7 天' },
  { key: 'last14d', label: '近 14 天' },
  { key: 'last30d', label: '近 30 天' },
  { key: 'thisMonth', label: '本月' },
  { key: 'lastMonth', label: '上月' }
]

/** 当前选中患者；未选中时回退到第一位患者。 */
const selected = computed(() => store.patients.find((item) => item.id === activeId.value) ?? store.patients[0])

onMounted(() => {
  document.addEventListener('pointerdown', handleDocumentPointerDown)
  loadPatientDashboard(true)
})

onBeforeUnmount(() => {
  document.removeEventListener('pointerdown', handleDocumentPointerDown)
})

/** 切换患者时自动刷新右侧订单列表。 */
watch(activeId, () => {
  loadOrders()
})

/** 日期弹层展开时同步当前已应用的筛选值。 */
watch(datePanelOpen, (visible) => {
  if (!visible) return
  dateDraft.startDate = filters.startDate
  dateDraft.endDate = filters.endDate
  dateDraft.preset = matchedPreset(filters.startDate, filters.endDate)
})

/** 打开日期范围弹层。 */
function showDatePanel() {
  if (datePanelOpen.value) return
  datePanelOpen.value = true
}

/** 点击日期控件外部时关闭日期面板。 */
function handleDocumentPointerDown(event: PointerEvent) {
  if (!datePanelOpen.value) return
  const target = event.target as HTMLElement | null
  if (target && dateRangeRef.value?.contains(target)) return
  if (target?.closest('.el-picker-panel')) return
  commitDateDraft()
  datePanelOpen.value = false
}

/** 按当前过滤条件搜索患者并刷新订单列表。 */
async function search() {
  if (searching.value) return
  searching.value = true
  try {
    await loadPatientDashboard(false)
  } finally {
    searching.value = false
  }
}

/** 清空过滤条件并重新搜索。 */
async function reset() {
  if (searching.value) return
  filters.keyword = ''
  filters.startDate = ''
  filters.endDate = ''
  dateDraft.startDate = ''
  dateDraft.endDate = ''
  dateDraft.preset = ''
  datePanelOpen.value = false
  await search()
}

/** 加载患者和订单数据；初始化时保留仍存在的当前选中患者。 */
async function loadPatientDashboard(keepCurrent: boolean) {
  const previousId = activeId.value
  await store.load({
    keyword: filters.keyword,
    startDate: filters.startDate,
    endDate: filters.endDate
  })
  const nextId = keepCurrent && store.patients.some((patient) => patient.id === previousId)
    ? previousId
    : store.patients[0]?.id ?? 0
  if (activeId.value === nextId) {
    await loadOrders()
  } else {
    activeId.value = nextId
  }
}

/** 打开新建或编辑患者资料页。 */
function openBasic(id?: number) {
  router.push(id ? `/basic/${id}` : '/basic')
}

/** 打开指定订单关联的患者资料页。 */
function openOrderBasic(patientId: number, orderId: number) {
  router.push(`/basic/${patientId}?orderId=${orderId}`)
}

/** 打开指定患者订单的拍摄页。 */
function openShoot(patientId: number, orderId: number) {
  router.push(`/shoot/${patientId}?orderId=${orderId}`)
}

/** 将日期格式化为后端查询使用的 YYYY-MM-DD。 */
function formatDate(value: Date) {
  const year = value.getFullYear()
  const month = String(value.getMonth() + 1).padStart(2, '0')
  const day = String(value.getDate()).padStart(2, '0')
  return `${year}-${month}-${day}`
}

/** 返回一个去掉时分秒的本地日期。 */
function startOfDay(value = new Date()) {
  return new Date(value.getFullYear(), value.getMonth(), value.getDate())
}

/** 在本地日期上增减天数。 */
function addDays(value: Date, days: number) {
  const next = new Date(value)
  next.setDate(next.getDate() + days)
  return next
}

/** 根据快捷项计算起止日期。 */
function rangeForPreset(key: string) {
  const today = startOfDay()
  if (key === 'today' || key === 'last24h') {
    return { startDate: formatDate(today), endDate: formatDate(today) }
  }
  if (key === 'yesterday') {
    const yesterday = addDays(today, -1)
    return { startDate: formatDate(yesterday), endDate: formatDate(yesterday) }
  }
  if (key === 'last7d') {
    return { startDate: formatDate(addDays(today, -6)), endDate: formatDate(today) }
  }
  if (key === 'last14d') {
    return { startDate: formatDate(addDays(today, -13)), endDate: formatDate(today) }
  }
  if (key === 'last30d') {
    return { startDate: formatDate(addDays(today, -29)), endDate: formatDate(today) }
  }
  if (key === 'thisMonth') {
    return { startDate: formatDate(new Date(today.getFullYear(), today.getMonth(), 1)), endDate: formatDate(today) }
  }
  if (key === 'lastMonth') {
    const firstDay = new Date(today.getFullYear(), today.getMonth() - 1, 1)
    const lastDay = new Date(today.getFullYear(), today.getMonth(), 0)
    return { startDate: formatDate(firstDay), endDate: formatDate(lastDay) }
  }
  return { startDate: '', endDate: '' }
}

/** 选择快捷日期范围。 */
function chooseDatePreset(key: string) {
  const range = rangeForPreset(key)
  dateDraft.startDate = range.startDate
  dateDraft.endDate = range.endDate
  dateDraft.preset = key
}

/** 自定义日期变更后清除快捷项高亮。 */
function handleDraftDateChange() {
  dateDraft.preset = matchedPreset(dateDraft.startDate, dateDraft.endDate)
}

/** 将日期草稿写入当前筛选条件。 */
function commitDateDraft() {
  if (dateDraft.startDate && dateDraft.endDate && dateDraft.startDate > dateDraft.endDate) {
    const previousStart = dateDraft.startDate
    dateDraft.startDate = dateDraft.endDate
    dateDraft.endDate = previousStart
  }
  filters.startDate = dateDraft.startDate
  filters.endDate = dateDraft.endDate
}

/** 应用日期范围筛选。 */
async function applyDateRange() {
  if (searching.value) return
  commitDateDraft()
  datePanelOpen.value = false
  await search()
}

/** 日期触发器展示文案。 */
const dateRangeLabel = computed(() => {
  const preset = matchedPreset(filters.startDate, filters.endDate)
  if (preset) {
    return datePresets.find((item) => item.key === preset)?.label ?? '创建日期'
  }
  if (filters.startDate && filters.endDate) {
    return filters.startDate === filters.endDate ? filters.startDate : `${filters.startDate} - ${filters.endDate}`
  }
  if (filters.startDate) return `${filters.startDate} 起`
  if (filters.endDate) return `${filters.endDate} 止`
  return '创建日期'
})

/** 查找与当前范围完全一致的快捷项。 */
function matchedPreset(startDate: string, endDate: string) {
  return datePresets.find((item) => {
    const range = rangeForPreset(item.key)
    return range.startDate === startDate && range.endDate === endDate
  })?.key ?? ''
}

/** 返回订单点云预览图 URL；未生成点云时返回空。 */
function orderPreviewUrl(order: Order) {
  return order.plyPath && order.previewPath ? imageFileUrl(order.previewPath) : ''
}

/** 返回患者列表缩略图 URL；加载失败时由默认头像兜底。 */
function patientThumbnailUrl(patient: { id: number; thumbnailPath?: string }) {
  if (!patient.thumbnailPath || brokenThumbnails.value[patient.id]) return ''
  return imageFileUrl(patient.thumbnailPath)
}

/** 记录不可展示的患者缩略图。 */
function markThumbnailBroken(patientId: number) {
  brokenThumbnails.value = { ...brokenThumbnails.value, [patientId]: true }
}

/** 返回订单最后修改时间；兼容旧接口数据时回退到创建时间。 */
function orderUpdatedAt(order: Order) {
  return order.updatedAt || order.createdAt || '暂无修改时间'
}

/** 返回缩略图占位状态文案。 */
function orderScanSummary(order: Order) {
  if (order.plyPath) return '已生成点云截图'
  return order.scanCount ? `已保存 ${order.scanCount} 张图像` : '尚未保存图像'
}

/** 加载当前患者订单。 */
async function loadOrders() {
  if (!activeId.value) {
    orders.value = []
    return
  }
  ordersLoading.value = true
  try {
    orders.value = await fetchOrders(activeId.value)
  } finally {
    ordersLoading.value = false
  }
}

/** 为患者新增订单并刷新订单列表。 */
async function addOrder(patientId: number) {
  const order = await createOrder(patientId)
  ElMessage.success(`订单 ${order.orderNo} 已生成`)
  activeId.value = patientId
  await loadOrders()
}

/** 二次确认后删除患者及其全部订单数据。 */
async function removePatient(patientId: number) {
  try {
    await ElMessageBox.confirm('删除患者会同步删除其全部订单和采图记录，是否继续？', '删除患者', {
      type: 'warning',
      confirmButtonText: '删除',
      cancelButtonText: '取消'
    })
  } catch {
    return
  }
  await deletePatient(patientId)
  ElMessage.success('患者已删除')
  await store.load(filters)
  activeId.value = store.patients[0]?.id ?? 0
  await loadOrders()
}

/** 二次确认后删除单个订单。 */
async function removeOrder(order: Order) {
  try {
    await ElMessageBox.confirm(`确认删除订单 ${order.orderNo}？`, '删除订单', {
      type: 'warning',
      confirmButtonText: '删除',
      cancelButtonText: '取消'
    })
  } catch {
    return
  }
  await deleteOrder(order.id)
  ElMessage.success('订单已删除')
  await loadOrders()
}

/** 请求后端在系统文件管理器中打开订单目录。 */
async function revealOrderFolder(order: Order) {
  const result = await openOrderFolder(order.id)
  if (result.ok) {
    ElMessage.success(`已打开文件夹：${result.path}`)
    return
  }
  ElMessage.warning('未能打开文件夹，请检查本机文件管理器设置')
}
</script>

<template>
  <main class="browse-page">
    <header class="browse-titlebar">
      <LogoMark />
      <div class="app-title">
        <strong>面扫采集工作台</strong>
        <span>患者检索 · 扫描记录 · 快速拍摄</span>
      </div>
      <WindowIcons />
    </header>

    <section class="browse-toolbar">
      <el-input v-model="filters.keyword" class="keyword-input" placeholder="患者编号 / 姓名 / 医生" clearable />
      <div ref="dateRangeRef" class="date-range-control">
        <button
          class="date-range-trigger"
          :class="{ active: datePanelOpen || filters.startDate || filters.endDate }"
          type="button"
          :aria-expanded="datePanelOpen"
          @click.stop="showDatePanel"
        >
          <CalendarDays :size="18" />
          <span>{{ dateRangeLabel }}</span>
          <ChevronDown :size="17" class="date-chevron" :class="{ open: datePanelOpen }" />
        </button>
        <section v-if="datePanelOpen" class="date-range-panel" @click.stop>
          <div class="date-preset-grid">
            <button
              v-for="preset in datePresets"
              :key="preset.key"
              class="date-preset"
              :class="{ selected: dateDraft.preset === preset.key }"
              type="button"
              @click="chooseDatePreset(preset.key)"
            >
              {{ preset.label }}
            </button>
          </div>
          <div class="date-range-divider" />
          <div class="date-range-fields">
            <label>
              <span>开始日期</span>
              <el-date-picker
                v-model="dateDraft.startDate"
                class="range-date-input"
                type="date"
                value-format="YYYY-MM-DD"
                placeholder="开始日期"
                @change="handleDraftDateChange"
              />
            </label>
            <span class="date-range-arrow">→</span>
            <label>
              <span>结束日期</span>
              <el-date-picker
                v-model="dateDraft.endDate"
                class="range-date-input"
                type="date"
                value-format="YYYY-MM-DD"
                placeholder="结束日期"
                @change="handleDraftDateChange"
              />
            </label>
          </div>
          <footer class="date-range-actions">
            <button class="primary-btn date-apply-btn" type="button" @click="applyDateRange">应用</button>
          </footer>
        </section>
      </div>
      <el-button class="soft-btn" @click="search">
        <Search :size="17" />搜索
      </el-button>
      <el-button class="soft-btn" @click="reset">
        <RotateCcw :size="17" />重置
      </el-button>
      <el-button class="primary-btn new-patient" @click="openBasic()">
        <UserPlus :size="18" />新建患者
      </el-button>
    </section>

    <section class="browse-content">
      <aside class="patient-list" v-loading="store.loading">
        <div class="panel-title">
          <span>患者列表</span>
          <small>{{ store.patients.length }} 位</small>
        </div>
        <article
          v-for="patient in store.patients"
          :key="patient.id"
          class="patient-row"
          :class="{ active: activeId === patient.id }"
          role="button"
          tabindex="0"
          @click="activeId = patient.id"
          @keyup.enter="activeId = patient.id"
        >
          <div class="thumb">
            <img
              v-if="patientThumbnailUrl(patient)"
              :src="patientThumbnailUrl(patient)"
              :alt="`${patient.name || '患者'}正面图`"
              @error="markThumbnailBroken(patient.id)"
            />
            <UserRound v-else :size="22" />
          </div>
          <div class="patient-summary">
            <strong>{{ patient.name || '姓名' }}，{{ patient.gender || '性别' }}</strong>
            <span>{{ patient.patientNo || '患者编号' }}</span>
            <span><CalendarDays :size="13" />{{ patient.createdAt || '创建时间' }}</span>
          </div>
          <div class="patient-actions">
            <button class="icon-round-btn" title="新增订单" type="button" @click.stop="addOrder(patient.id)">
              <CirclePlus :size="21" />
            </button>
            <button class="icon-round-btn danger" title="删除患者" type="button" @click.stop="removePatient(patient.id)">
              <Trash2 :size="18" />
            </button>
          </div>
        </article>
        <div v-if="!store.loading && !store.patients.length" class="empty-state">暂无患者记录</div>
      </aside>

      <section class="scan-workspace" v-loading="ordersLoading">
        <template v-if="selected">
          <header class="workspace-header">
            <div>
              <span class="eyebrow">当前患者</span>
              <h2>{{ selected.name }} · {{ selected.patientNo }}</h2>
            </div>
            <div class="patient-meta">
              <span><Stethoscope :size="16" />{{ selected.doctor || '未分配医生' }}</span>
              <span>{{ selected.age || '-' }} 岁</span>
            </div>
          </header>
          <article v-for="order in orders" :key="order.id" class="scan-card">
            <header>
              <span>{{ order.orderNo }}</span>
              <div class="scan-actions">
                <button class="icon-round-btn" title="打开文件夹" type="button" @click="revealOrderFolder(order)"><FolderOpen :size="18" /></button>
                <button class="icon-round-btn danger" title="删除订单" type="button" @click="removeOrder(order)"><Trash2 :size="18" /></button>
                <button class="icon-round-btn" title="编辑信息" type="button" @click="openOrderBasic(selected.id, order.id)"><Edit :size="18" /></button>
              </div>
            </header>
            <button class="scan-preview" type="button" @click="openShoot(selected.id, order.id)">
              <img v-if="orderPreviewUrl(order)" class="scan-preview-image" :src="orderPreviewUrl(order)" :alt="`${order.orderNo} 点云正面截图`" />
              <span v-else class="scan-placeholder">
                <ClipboardList :size="28" />
                <strong>{{ order.scanCount ? '等待点云预览' : '等待采集预览' }}</strong>
                <small>{{ orderScanSummary(order) }}</small>
              </span>
            </button>
            <div class="scan-time">
              <CalendarDays :size="16" />
              <span>{{ orderUpdatedAt(order) }}</span>
              <small>最后修改</small>
            </div>
          </article>
          <div v-if="!ordersLoading && !orders.length" class="empty-state order-empty">暂无订单，请在左侧患者卡片新增订单</div>
        </template>
      </section>
    </section>
  </main>
</template>
