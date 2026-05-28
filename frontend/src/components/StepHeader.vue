<script setup lang="ts">
import { computed, nextTick, onBeforeUnmount, onMounted, ref, watch } from 'vue'
import { useRoute, useRouter } from 'vue-router'
import { Camera, Check, ClipboardList, Send, Sparkles } from 'lucide-vue-next'
import { ElMessage } from 'element-plus'
import LogoMark from './LogoMark.vue'
import WindowIcons from './WindowIcons.vue'

/** 记录上次进度条宽度，避免页面切换时进度条从 0 闪烁。 */
let lastProgressPx = 0

/** 当前流程步骤。 */
const props = defineProps<{
  active: 'basic' | 'shoot' | 'process' | 'send'
}>()

const router = useRouter()
const route = useRoute()

/** 工作流步骤配置，图标组件直接供模板渲染。 */
const steps = [
  { key: 'basic', label: '基本信息', icon: ClipboardList },
  { key: 'shoot', label: '拍摄', icon: Camera },
  { key: 'process', label: '三维处理', icon: Sparkles },
  { key: 'send', label: '发送', icon: Send }
] as const

/** 当前步骤索引。 */
const activeIndex = computed(() => steps.findIndex((step) => step.key === props.active))
/** 头部容器引用，用于测量进度条宽度。 */
const headerRef = ref<HTMLElement>()
/** 各步骤按钮引用，用于计算当前步骤中心点。 */
const stepRefs = ref<HTMLElement[]>([])
/** 进度条像素宽度。 */
const progressPx = ref(lastProgressPx)
/** 进度条 CSS 宽度。 */
const progressWidth = computed(() => `${progressPx.value}px`)

/** 保存 v-for 中每个步骤按钮的 DOM 引用。 */
function setStepRef(el: unknown, index: number) {
  if (el instanceof HTMLElement) {
    stepRefs.value[index] = el
  }
}

/** 根据当前步骤按钮位置刷新进度条宽度。 */
async function updateProgress() {
  await nextTick()
  const header = headerRef.value
  const step = stepRefs.value[activeIndex.value]
  if (!header || !step) {
    return
  }
  const headerRect = header.getBoundingClientRect()
  const stepRect = step.getBoundingClientRect()
  progressPx.value = Math.max(0, stepRect.left + stepRect.width / 2 - headerRect.left)
  lastProgressPx = progressPx.value
}

/** 返回步骤视觉状态。 */
function stepState(index: number) {
  if (index < activeIndex.value) {
    return 'done'
  }
  if (index === activeIndex.value) {
    return 'active'
  }
  return 'todo'
}

/** 根据步骤跳转到对应流程页，并保留订单上下文。 */
function goStep(key: string) {
  const patientId = route.params.id
  const orderId = Number(route.query.orderId || 0)
  const orderQuery = orderId ? `?orderId=${orderId}` : ''
  if (key === 'basic') {
    router.push(patientId ? `/basic/${patientId}${orderQuery}` : '/basic')
    return
  }
  if (!patientId && key !== 'basic') {
    ElMessage.warning('请先选择或创建患者订单')
    return
  }
  if (key === 'shoot') {
    router.push(`/shoot/${patientId}${orderQuery}`)
    return
  }
  if (key === 'send') {
    router.push(`/send/${patientId}${orderQuery}`)
    return
  }
  if (key === 'process') {
    router.push(`/pointcloud/${patientId}${orderQuery}`)
    return
  }
}

/** 首次渲染后测量进度条并监听窗口尺寸变化。 */
onMounted(() => {
  updateProgress()
  window.addEventListener('resize', updateProgress)
})

/** 移除窗口尺寸监听。 */
onBeforeUnmount(() => {
  window.removeEventListener('resize', updateProgress)
})

/** 当前步骤变化时重新测量进度条。 */
watch(activeIndex, updateProgress)
</script>

<template>
  <header ref="headerRef" class="step-header">
    <LogoMark />
    <nav class="step-nav" aria-label="订单流程">
      <div class="step-track">
        <span class="step-progress" :style="{ width: progressWidth }" />
      </div>
      <button
        v-for="(step, index) in steps"
        :key="step.key"
        :ref="(el) => setStepRef(el, index)"
        class="workflow-step"
        :class="stepState(index)"
        type="button"
        @click="goStep(step.key)"
      >
        <span class="step-copy">
          <span class="step-label">{{ step.label }}</span>
        </span>
        <span class="step-icon">
          <Check v-if="stepState(index) === 'done'" :size="15" />
          <component :is="step.icon" v-else :size="15" />
        </span>
      </button>
    </nav>
    <WindowIcons />
  </header>
</template>
