<script setup lang="ts">
import { ref } from 'vue'
import { useRouter } from 'vue-router'
import { ElMessage } from 'element-plus'
import { login } from '../api/client'
import LogoMark from '../components/LogoMark.vue'

const router = useRouter()
const username = ref('admin')
const password = ref('admin')
const loading = ref(false)

async function submit() {
  loading.value = true
  try {
    await login(username.value, password.value)
    router.push('/browse')
  } catch (error) {
    ElMessage.error('登录失败，请检查后端服务')
  } finally {
    loading.value = false
  }
}
</script>

<template>
  <main class="login-page">
    <section class="login-box">
      <LogoMark />
      <h1>面扫系统</h1>
      <el-input v-model="username" size="large" placeholder="用户名" />
      <el-input v-model="password" size="large" placeholder="密码" show-password @keyup.enter="submit" />
      <el-button type="primary" size="large" :loading="loading" @click="submit">登录</el-button>
    </section>
  </main>
</template>

