<script setup lang="ts">
import { Camera, Home, LogOut, Settings } from 'lucide-vue-next'
import { ElMessage } from 'element-plus'
import { useRoute, useRouter } from 'vue-router'

const router = useRouter()
const route = useRoute()

/** 通过浏览器屏幕捕获 API 截取当前页面并下载为 PNG。 */
async function takeScreenshot() {
  if (!navigator.mediaDevices?.getDisplayMedia) {
    ElMessage.warning('当前浏览器不支持网页截图，请使用系统截图工具')
    return
  }

  try {
    const stream = await navigator.mediaDevices.getDisplayMedia({
      video: { displaySurface: 'browser' },
      audio: false
    })
    const video = document.createElement('video')
    video.srcObject = stream
    await video.play()

    const canvas = document.createElement('canvas')
    canvas.width = video.videoWidth
    canvas.height = video.videoHeight
    const context = canvas.getContext('2d')
    context?.drawImage(video, 0, 0)
    stream.getTracks().forEach((track) => track.stop())

    const link = document.createElement('a')
    link.download = `facescan-${new Date().toISOString().replace(/[:.]/g, '-')}.png`
    link.href = canvas.toDataURL('image/png')
    link.click()
    ElMessage.success('截图已生成')
  } catch (error) {
    ElMessage.info('截图已取消')
  }
}

/** 清除本地 token 并返回登录页。 */
function logout() {
  localStorage.removeItem('facescan.token')
  ElMessage.success('已退出登录')
  router.push('/login')
}

/** 打开设置页；从首页进入时允许编辑数据目录。 */
function openSettings() {
  router.push({
    path: '/settings',
    query: route.path === '/browse' ? { from: 'browse' } : {}
  })
}
</script>

<template>
  <div class="window-icons" aria-label="页面工具栏">
    <button class="toolbar-btn" data-tooltip="截图当前页面" aria-label="截图当前页面" type="button" @click="takeScreenshot">
      <Camera :size="21" />
    </button>
    <button class="toolbar-btn" data-tooltip="返回工作台" aria-label="返回工作台" type="button" @click="router.push('/browse')">
      <Home :size="21" />
    </button>
    <button class="toolbar-btn" data-tooltip="系统设置" aria-label="系统设置" type="button" @click="openSettings">
      <Settings :size="21" />
    </button>
    <button class="toolbar-btn ghost-danger" data-tooltip="退出登录" aria-label="退出登录" type="button" @click="logout">
      <LogOut :size="21" />
    </button>
  </div>
</template>
