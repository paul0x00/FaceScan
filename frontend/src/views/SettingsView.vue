<script setup lang="ts">
import { X } from 'lucide-vue-next'
import { useRouter } from 'vue-router'

const router = useRouter()
const sections = ['通用', '信息管理', '校准', '拍摄', '三维处理', '关于']
</script>

<template>
  <main class="settings-page">
    <header class="settings-header">
      <h1>设置</h1>
      <button class="icon-btn" title="关闭" type="button" @click="router.back()"><X :size="20" /></button>
    </header>
    <section class="settings-body">
      <nav class="settings-nav">
        <button v-for="section in sections" :key="section" type="button">{{ section }}</button>
      </nav>
      <div class="settings-content" />
    </section>
  </main>
</template>

