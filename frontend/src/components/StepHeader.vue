<script setup lang="ts">
import { computed, nextTick, onBeforeUnmount, onMounted, ref, watch } from 'vue'
import { useRoute, useRouter } from 'vue-router'
import { Camera, Check, ClipboardList, Send, Sparkles } from 'lucide-vue-next'
import { ElMessage } from 'element-plus'
import LogoMark from './LogoMark.vue'
import WindowIcons from './WindowIcons.vue'

let lastProgressPx = 0

const props = defineProps<{
  active: 'basic' | 'shoot' | 'process' | 'send'
}>()

const router = useRouter()
const route = useRoute()

const steps = [
  { key: 'basic', label: '基本信息', icon: ClipboardList },
  { key: 'shoot', label: '拍摄', icon: Camera },
  { key: 'process', label: '三维处理', icon: Sparkles },
  { key: 'send', label: '发送', icon: Send }
] as const

const activeIndex = computed(() => steps.findIndex((step) => step.key === props.active))
const headerRef = ref<HTMLElement>()
const stepRefs = ref<HTMLElement[]>([])
const progressPx = ref(lastProgressPx)
const progressWidth = computed(() => `${progressPx.value}px`)

function setStepRef(el: unknown, index: number) {
  if (el instanceof HTMLElement) {
    stepRefs.value[index] = el
  }
}

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

function stepState(index: number) {
  if (index < activeIndex.value) {
    return 'done'
  }
  if (index === activeIndex.value) {
    return 'active'
  }
  return 'todo'
}

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

onMounted(() => {
  updateProgress()
  window.addEventListener('resize', updateProgress)
})

onBeforeUnmount(() => {
  window.removeEventListener('resize', updateProgress)
})

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
