<script setup lang="ts">
import { computed, onMounted, ref } from 'vue'
import {
  Camera,
  CheckCircle2,
  Cpu,
  Database,
  FolderOpen,
  FolderSearch,
  Info,
  RefreshCw,
  Save,
  Settings,
  ShieldCheck,
  SlidersHorizontal,
  X
} from 'lucide-vue-next'
import { useRoute, useRouter } from 'vue-router'
import { ElMessage, ElMessageBox } from 'element-plus'
import { fetchSettings, openDataRoot, saveSettings, selectDataRoot, validateDataRoot } from '../api/client'
import type { AppSettings } from '../types'

const router = useRouter()
const route = useRoute()
/** 当前设置分区。 */
const active = ref('通用')
/** 设置加载状态。 */
const loading = ref(false)
/** 设置保存状态。 */
const saving = ref(false)
/** 打开数据目录状态。 */
const openingRoot = ref(false)
/** 设置读取或保存错误。 */
const errorMessage = ref('')
/** 后端返回的系统设置。 */
const settings = ref<AppSettings>({
  dataRoot: '',
  databasePath: '',
  configPath: '',
  cameraMode: ''
})
/** 数据根目录输入草稿，保存前不直接改动 settings。 */
const dataRootDraft = ref('')
/** 设置页左侧分区导航配置。 */
const sections = [
  { label: '通用', icon: Settings },
  { label: '信息管理', icon: Database },
  { label: '校准', icon: SlidersHorizontal },
  { label: '拍摄', icon: Camera },
  { label: '三维处理', icon: SlidersHorizontal },
  { label: '关于', icon: Info }
]
/** 只有从首页进入设置时允许修改数据目录。 */
const canEditDataRoot = computed(() => route.query.from === 'browse')
/** 当前路径草稿是否有未保存变更。 */
const dataRootChanged = computed(() => dataRootDraft.value.trim() !== settings.value.dataRoot)
/** 当前激活分区配置。 */
const activeSection = computed(() => sections.find((section) => section.label === active.value) ?? sections[0])

/** 读取可展示的相机模式名称。 */
const cameraModeText = computed(() => {
  const mode = settings.value.cameraMode || 'mock'
  if (mode === 'mock') return '模拟相机'
  if (mode === 'orbbec' || mode === 'gemini215') return 'Orbbec Gemini 215'
  return mode
})

onMounted(loadSettings)

/** 从后端读取系统设置并同步输入草稿。 */
async function loadSettings() {
  loading.value = true
  errorMessage.value = ''
  try {
    settings.value = await fetchSettings()
    dataRootDraft.value = settings.value.dataRoot
  } catch (err: any) {
    errorMessage.value = err?.response?.data?.error || '系统设置读取失败'
    ElMessage.error(errorMessage.value)
  } finally {
    loading.value = false
  }
}

/** 校验并保存数据根目录，必要时确认患者文件迁移。 */
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
    errorMessage.value = err?.response?.data?.message || '目录路径不合法'
    ElMessage.warning(errorMessage.value)
    return
  }
  dataRootDraft.value = validation.normalizedPath
  if (validation.needsMigration) {
    try {
      await ElMessageBox.confirm(
        `原目录：${settings.value.dataRoot || '未读取'}\n新目录：${validation.normalizedPath}\n\n此操作会迁移此前的患者数据文件夹，数据库仍保留在后端固定目录。数据量较大时可能耗费较长时间。是否确认继续？`,
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
  errorMessage.value = ''
  try {
    settings.value = await saveSettings({ dataRoot: validation.normalizedPath })
    dataRootDraft.value = settings.value.dataRoot
    ElMessage.success(validation.needsMigration ? '数据保存目录已更新，患者数据文件夹已迁移' : '数据保存目录已保存')
  } catch (err: any) {
    errorMessage.value = err?.response?.data?.error || '数据保存目录保存失败'
    ElMessage.error(errorMessage.value)
  } finally {
    saving.value = false
  }
}

/** 调用系统目录选择器并回填路径。 */
async function chooseRoot() {
  if (!canEditDataRoot.value) {
    ElMessage.warning('请从首页进入设置后再选择数据保存目录')
    return
  }
  try {
    const result = await selectDataRoot()
    if (result.ok) {
      dataRootDraft.value = result.path
    } else {
      ElMessage.info('未选择目录')
    }
  } catch (err: any) {
    ElMessage.error(err?.response?.data?.error || '目录选择器打开失败')
  }
}

/** 在系统文件管理器中打开当前数据目录。 */
async function revealRoot() {
  openingRoot.value = true
  try {
    const result = await openDataRoot()
    if (result.ok) {
      ElMessage.success('已打开当前数据目录')
    } else {
      ElMessage.warning('当前数据目录无法打开')
    }
  } catch (err: any) {
    ElMessage.error(err?.response?.data?.error || '打开数据目录失败')
  } finally {
    openingRoot.value = false
  }
}
</script>

<template>
  <main class="settings-page">
    <header class="settings-header">
      <h1>设置</h1>
      <button class="icon-btn" title="关闭" aria-label="关闭设置" type="button" @click="router.back()"><X :size="20" /></button>
    </header>
    <section class="settings-body">
      <nav class="settings-nav" aria-label="设置分区">
        <button
          v-for="section in sections"
          :key="section.label"
          :class="{ active: active === section.label }"
          :aria-current="active === section.label ? 'page' : undefined"
          type="button"
          @click="active = section.label"
        >
          <component :is="section.icon" :size="17" />
          {{ section.label }}
        </button>
      </nav>
      <div class="settings-content" v-loading="loading">
        <div class="settings-title-row">
          <div>
            <span class="eyebrow">系统设置</span>
            <h2>{{ activeSection.label }}</h2>
          </div>
          <button class="soft-btn nowrap-btn" type="button" :disabled="loading" @click="loadSettings">
            <RefreshCw :size="17" />刷新
          </button>
        </div>
        <p v-if="errorMessage" class="settings-alert" role="alert">{{ errorMessage }}</p>
        <section v-if="active === '通用'" class="settings-section">
          <div v-if="!canEditDataRoot" class="settings-notice">
            <ShieldCheck :size="18" />
            <span>当前为只读设置视图。需要修改数据保存目录时，请从工作台进入设置。</span>
            <button class="soft-btn compact-btn" type="button" @click="router.push('/browse')">返回工作台</button>
          </div>
          <div class="directory-setting">
            <header>
              <div>
                <strong>数据保存目录</strong>
                <span>{{ canEditDataRoot ? '拍摄图片、点云、缩略图和压缩包会保存到该目录下' : '当前页面仅可查看路径，请从首页进入设置后修改' }}</span>
              </div>
              <div class="settings-actions">
                <button class="soft-btn nowrap-btn" type="button" :disabled="openingRoot || !settings.dataRoot" @click="revealRoot">
                  <FolderSearch :size="17" />打开目录
                </button>
                <button class="soft-btn nowrap-btn" type="button" :disabled="!canEditDataRoot" @click="chooseRoot">
                  <FolderOpen :size="17" />选择目录
                </button>
              </div>
            </header>
            <label class="settings-field-label" for="data-root-input">本机数据保存目录</label>
            <div class="settings-path-row">
              <el-input id="data-root-input" v-model="dataRootDraft" :readonly="!canEditDataRoot" :class="{ 'readonly-control': !canEditDataRoot }" placeholder="请选择或输入本机数据保存目录" />
              <button class="soft-btn nowrap-btn" type="button" :disabled="saving || !canEditDataRoot || !dataRootChanged" @click="saveDataRoot">
                <Save :size="17" />保存
              </button>
            </div>
            <div class="settings-status-row">
              <span :class="{ changed: dataRootChanged }">{{ dataRootChanged ? '有未保存的目录变更' : '目录设置已同步' }}</span>
              <span>{{ cameraModeText }} · 本地数据库</span>
            </div>
            <div class="settings-path-meta">
              <span>当前目录：{{ settings.dataRoot || '未读取' }}</span>
              <span>数据库：{{ settings.databasePath || '未读取' }}</span>
              <span>配置文件：{{ settings.configPath || '未读取' }}</span>
            </div>
          </div>
        </section>
        <section v-else-if="active === '信息管理'" class="settings-grid">
          <article>
            <Database :size="20" />
            <strong>数据库</strong>
            <span>{{ settings.databasePath || '未读取' }}</span>
          </article>
          <article>
            <FolderSearch :size="20" />
            <strong>患者数据目录</strong>
            <span>{{ settings.dataRoot || '未读取' }}</span>
          </article>
          <article class="muted-card">
            <ShieldCheck :size="20" />
            <strong>备份与索引</strong>
            <span>数据库备份、患者索引重扫和导出入口待开放</span>
          </article>
        </section>
        <section v-else-if="active === '校准'" class="settings-grid">
          <article>
            <SlidersHorizontal :size="20" />
            <strong>校准状态</strong>
            <span>当前未配置独立校准文件</span>
          </article>
          <article>
            <CheckCircle2 :size="20" />
            <strong>默认流程</strong>
            <span>使用设备或模拟相机默认参数</span>
          </article>
          <article class="muted-card">
            <FolderOpen :size="20" />
            <strong>参数导入导出</strong>
            <span>校准参数管理入口待开放</span>
          </article>
        </section>
        <section v-else-if="active === '拍摄'" class="settings-grid">
          <article>
            <Camera :size="20" />
            <strong>相机模式</strong>
            <span>{{ cameraModeText }}</span>
          </article>
          <article>
            <SlidersHorizontal :size="20" />
            <strong>曝光控制</strong>
            <span>自动曝光、曝光、增益和亮度在拍摄页调节</span>
          </article>
          <article class="muted-card">
            <CheckCircle2 :size="20" />
            <strong>设备检测</strong>
            <span>真实相机连接检测入口待开放</span>
          </article>
        </section>
        <section v-else-if="active === '三维处理'" class="settings-grid">
          <article>
            <Cpu :size="20" />
            <strong>重建采样</strong>
            <span>默认 96 × 72，输出 PLY 点云</span>
          </article>
          <article>
            <FolderSearch :size="20" />
            <strong>输出位置</strong>
            <span>点云文件保存到对应患者订单目录</span>
          </article>
          <article class="muted-card">
            <SlidersHorizontal :size="20" />
            <strong>质量预设</strong>
            <span>采样密度和预览质量设置待开放</span>
          </article>
        </section>
        <section v-else class="settings-grid">
          <article>
            <Info :size="20" />
            <strong>FaceScan</strong>
            <span>第一阶段 MVP</span>
          </article>
          <article>
            <Camera :size="20" />
            <strong>设备模式</strong>
            <span>{{ settings.cameraMode || 'mock' }}</span>
          </article>
          <article>
            <Database :size="20" />
            <strong>运行数据</strong>
            <span>本地 SQLite 数据库和本机患者文件目录</span>
          </article>
        </section>
      </div>
    </section>
  </main>
</template>
