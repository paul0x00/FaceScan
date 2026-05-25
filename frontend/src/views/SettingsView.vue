<script setup lang="ts">
import { computed, onMounted, ref } from 'vue'
import { Camera, Database, FolderOpen, Info, Save, Settings, SlidersHorizontal, X } from 'lucide-vue-next'
import { useRoute, useRouter } from 'vue-router'
import { ElMessage, ElMessageBox } from 'element-plus'
import { fetchSettings, saveSettings, selectDataRoot, validateDataRoot } from '../api/client'
import type { AppSettings } from '../types'

const router = useRouter()
const route = useRoute()
const active = ref('通用')
const loading = ref(false)
const saving = ref(false)
const settings = ref<AppSettings>({
  dataRoot: '',
  databasePath: '',
  configPath: '',
  cameraMode: ''
})
const dataRootDraft = ref('')
const sections = [
  { label: '通用', icon: Settings },
  { label: '信息管理', icon: Database },
  { label: '校准', icon: SlidersHorizontal },
  { label: '拍摄', icon: Camera },
  { label: '三维处理', icon: SlidersHorizontal },
  { label: '关于', icon: Info }
]
const canEditDataRoot = computed(() => route.query.from === 'browse')

onMounted(loadSettings)

async function loadSettings() {
  loading.value = true
  try {
    settings.value = await fetchSettings()
    dataRootDraft.value = settings.value.dataRoot
  } finally {
    loading.value = false
  }
}

async function saveDataRoot() {
  if (!canEditDataRoot.value) {
    ElMessage.warning('请从首页进入设置后再修改数据保存目录')
    return
  }
  const value = dataRootDraft.value.trim()
  if (!value) {
    ElMessage.warning('请输入数据保存目录')
    return
  }
  let validation: Awaited<ReturnType<typeof validateDataRoot>>
  try {
    validation = await validateDataRoot(value)
  } catch (err: any) {
    ElMessage.warning(err?.response?.data?.message || '目录路径不合法')
    return
  }
  dataRootDraft.value = validation.normalizedPath
  if (validation.needsMigration) {
    try {
      await ElMessageBox.confirm(
        '此操作会迁移此前的患者数据文件夹，数据库仍保留在后端固定目录。数据量较大时可能耗费较长时间。是否确认继续？',
        '确认迁移患者数据',
        {
          type: 'warning',
          confirmButtonText: '确认迁移',
          cancelButtonText: '取消'
        }
      )
    } catch {
      return
    }
  }
  saving.value = true
  try {
    settings.value = await saveSettings({ dataRoot: validation.normalizedPath })
    dataRootDraft.value = settings.value.dataRoot
    ElMessage.success(validation.needsMigration ? '数据保存目录已更新，患者数据文件夹已迁移' : '数据保存目录已保存')
  } finally {
    saving.value = false
  }
}

async function chooseRoot() {
  if (!canEditDataRoot.value) {
    ElMessage.warning('请从首页进入设置后再选择数据保存目录')
    return
  }
  const result = await selectDataRoot()
  if (result.ok) {
    dataRootDraft.value = result.path
  } else {
    ElMessage.info('未选择目录')
  }
}
</script>

<template>
  <main class="settings-page">
    <header class="settings-header">
      <h1>设置</h1>
      <button class="icon-btn" title="关闭" type="button" @click="router.back()"><X :size="20" /></button>
    </header>
    <section class="settings-body">
      <nav class="settings-nav">
        <button
          v-for="section in sections"
          :key="section.label"
          :class="{ active: active === section.label }"
          type="button"
          @click="active = section.label"
        >
          <component :is="section.icon" :size="17" />
          {{ section.label }}
        </button>
      </nav>
      <div class="settings-content" v-loading="loading">
        <span class="eyebrow">系统设置</span>
        <h2>{{ active }}</h2>
        <section v-if="active === '通用'" class="settings-section">
          <div class="directory-setting">
            <header>
              <div>
                <strong>数据保存目录</strong>
                <span>{{ canEditDataRoot ? '拍摄图片、点云、缩略图和压缩包会保存到该目录下' : '当前页面仅可查看路径，请从首页进入设置后修改' }}</span>
              </div>
              <button class="soft-btn nowrap-btn" type="button" :disabled="!canEditDataRoot" @click="chooseRoot">
                <FolderOpen :size="17" />选择目录
              </button>
            </header>
            <div class="settings-path-row">
              <el-input v-model="dataRootDraft" :readonly="!canEditDataRoot" :class="{ 'readonly-control': !canEditDataRoot }" placeholder="请选择或输入本机数据保存目录" />
              <button class="soft-btn nowrap-btn" type="button" :disabled="saving || !canEditDataRoot" @click="saveDataRoot">
                <Save :size="17" />保存
              </button>
            </div>
            <div class="settings-path-meta">
              <span>当前目录：{{ settings.dataRoot || '未读取' }}</span>
              <span>数据库：{{ settings.databasePath || '未读取' }}</span>
              <span>配置文件：{{ settings.configPath || '未读取' }}</span>
            </div>
          </div>
        </section>
        <div v-else class="settings-grid">
          <article>
            <strong>设备模式</strong>
            <span>{{ settings.cameraMode || 'mock' }} · 本地数据库</span>
          </article>
          <article>
            <strong>数据目录</strong>
            <span>{{ settings.dataRoot || '未读取' }}</span>
          </article>
          <article>
            <strong>第一阶段范围</strong>
            <span>登录、患者、订单、预览、采图、保存</span>
          </article>
        </div>
      </div>
    </section>
  </main>
</template>
