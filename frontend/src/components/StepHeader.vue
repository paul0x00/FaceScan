<script setup lang="ts">
import LogoMark from './LogoMark.vue'
import WindowIcons from './WindowIcons.vue'

defineProps<{
  active: 'basic' | 'shoot' | 'process' | 'send'
}>()

const steps = [
  { key: 'basic', label: '基本信息' },
  { key: 'shoot', label: '拍摄' },
  { key: 'process', label: '三维处理' },
  { key: 'send', label: '发送' }
] as const
</script>

<template>
  <header class="step-header">
    <LogoMark />
    <nav class="step-nav">
      <div v-for="step in steps" :key="step.key" class="step-item">
        <span class="step-label">{{ step.label }}</span>
        <span class="step-dot" :class="{ active: active === step.key }" />
      </div>
    </nav>
    <WindowIcons />
  </header>
</template>

