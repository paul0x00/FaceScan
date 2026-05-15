<script setup lang="ts">
import { ref } from 'vue'
import { Camera, Database, Info, Settings, SlidersHorizontal, X } from 'lucide-vue-next'
import { useRouter } from 'vue-router'

const router = useRouter()
const active = ref('通用')
const sections = [
  { label: '通用', icon: Settings },
  { label: '信息管理', icon: Database },
  { label: '校准', icon: SlidersHorizontal },
  { label: '拍摄', icon: Camera },
  { label: '三维处理', icon: SlidersHorizontal },
  { label: '关于', icon: Info }
]
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
      <div class="settings-content">
        <span class="eyebrow">系统设置</span>
        <h2>{{ active }}</h2>
        <div class="settings-grid">
          <article>
            <strong>设备模式</strong>
            <span>模拟相机 · 本地数据库</span>
          </article>
          <article>
            <strong>数据目录</strong>
            <span>backend/data/images · backend/data/db</span>
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
