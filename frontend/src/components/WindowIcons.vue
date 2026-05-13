<script setup lang="ts">
import { Home, Maximize2, Minus, Settings, Square, X } from 'lucide-vue-next'
import { useRouter } from 'vue-router'

const router = useRouter()
</script>

<template>
  <div class="window-icons">
    <button class="icon-btn" title="窗口" type="button"><Maximize2 :size="21" /></button>
    <button class="icon-btn" title="设置" type="button" @click="router.push('/settings')"><Settings :size="22" /></button>
    <button class="icon-btn" title="浏览" type="button" @click="router.push('/browse')"><Home :size="22" /></button>
    <button class="icon-btn" title="最小化" type="button"><Minus :size="21" /></button>
    <button class="icon-btn" title="还原" type="button"><Square :size="19" /></button>
    <button class="icon-btn" title="关闭" type="button"><X :size="20" /></button>
  </div>
</template>

