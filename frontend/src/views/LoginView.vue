<script setup lang="ts">
import { ref } from 'vue'
import { useRouter } from 'vue-router'
import { ElMessage } from 'element-plus'
import { LogIn, ShieldCheck } from 'lucide-vue-next'
import { login } from '../api/client'
import LogoMark from '../components/LogoMark.vue'

const router = useRouter()
/** 登录用户名，MVP 阶段默认 admin。 */
const username = ref('admin')
/** 登录密码，MVP 阶段默认 admin。 */
const password = ref('admin')
/** 登录请求进行中状态。 */
const loading = ref(false)

/** 提交登录并进入工作台。 */
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
      <div class="login-copy">
        <span class="eyebrow"><ShieldCheck :size="15" />本地工作站</span>
        <h1>面扫系统</h1>
        <p>患者建档、四路采集与扫描数据归档</p>
      </div>
      <el-input v-model="username" size="large" placeholder="用户名" />
      <el-input v-model="password" size="large" placeholder="密码" show-password @keyup.enter="submit" />
      <el-button class="primary-btn login-submit" size="large" :loading="loading" @click="submit">
        <LogIn :size="18" />登录工作台
      </el-button>
    </section>
  </main>
</template>
