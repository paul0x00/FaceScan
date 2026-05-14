<script setup lang="ts">
import { computed, onMounted, reactive, ref, watch } from 'vue'
import { useRouter } from 'vue-router'
import { CalendarDays, Camera, CirclePlus, ClipboardList, Edit, FolderOpen, RotateCcw, Search, Stethoscope, Trash2, UserPlus } from 'lucide-vue-next'
import { ElMessage, ElMessageBox } from 'element-plus'
import { createOrder, deleteOrder, deletePatient, fetchOrders, openOrderFolder } from '../api/client'
import { usePatientStore } from '../stores/patients'
import type { Order } from '../types'
import LogoMark from '../components/LogoMark.vue'
import WindowIcons from '../components/WindowIcons.vue'

const router = useRouter()
const store = usePatientStore()
const filters = reactive({
  keyword: '',
  date: ''
})
const activeId = ref(0)
const searching = ref(false)
const orders = ref<Order[]>([])
const ordersLoading = ref(false)

const selected = computed(() => store.patients.find((item) => item.id === activeId.value) ?? store.patients[0])

onMounted(async () => {
  await store.load()
  activeId.value = store.selected?.id ?? 0
  await loadOrders()
})

watch(activeId, () => {
  loadOrders()
})

async function search() {
  searching.value = true
  try {
    await store.load(filters)
    activeId.value = store.patients[0]?.id ?? 0
  } finally {
    searching.value = false
  }
}

async function reset() {
  filters.keyword = ''
  filters.date = ''
  await search()
}

function openBasic(id?: number) {
  router.push(id ? `/basic/${id}` : '/basic')
}

function openOrderBasic(patientId: number, orderId: number) {
  router.push(`/basic/${patientId}?orderId=${orderId}`)
}

function openShoot(patientId: number, orderId: number) {
  router.push(`/shoot/${patientId}?orderId=${orderId}`)
}

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

async function addOrder(patientId: number) {
  const order = await createOrder(patientId)
  ElMessage.success(`订单 ${order.orderNo} 已生成`)
  activeId.value = patientId
  await loadOrders()
}

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
      <el-date-picker v-model="filters.date" class="date-input" type="date" value-format="YYYY-MM-DD" placeholder="创建日期" />
      <el-button class="soft-btn" :loading="searching" @click="search">
        <Search :size="17" />搜索
      </el-button>
      <el-button class="soft-btn" :disabled="searching" @click="reset">
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
            <Camera :size="19" />
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
              <span class="scan-placeholder">
                <ClipboardList :size="28" />
                <strong>{{ order.createdAt }}</strong>
                <small>已保存 {{ order.scanCount }} 张图像</small>
              </span>
            </button>
          </article>
          <div v-if="!ordersLoading && !orders.length" class="empty-state order-empty">暂无订单，请在左侧患者卡片新增订单</div>
        </template>
      </section>
    </section>
  </main>
</template>
