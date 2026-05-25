<script setup lang="ts">
import { computed, onMounted, reactive, ref } from 'vue'
import { useRoute, useRouter } from 'vue-router'
import { ElMessage } from 'element-plus'
import { Check, X } from 'lucide-vue-next'
import FormPanel from '../components/FormPanel.vue'
import StepHeader from '../components/StepHeader.vue'
import { createPatient, updatePatient } from '../api/client'
import { usePatientStore } from '../stores/patients'
import type { PatientForm } from '../types'

const route = useRoute()
const router = useRouter()
const store = usePatientStore()
const id = computed(() => Number(route.params.id || 0))
const routeOrderId = computed(() => Number(route.query.orderId || 0))
const saving = ref(false)
const nameError = ref('')
const genderError = ref('')
const phoneError = ref('')

const form = reactive<PatientForm>({
  patientNo: '',
  orderNo: '',
  name: '',
  gender: '',
  age: null,
  phone: '',
  doctor: '',
  remark: ''
})

onMounted(async () => {
  if (!store.patients.length) {
    await store.load()
  }
  const patient = store.patients.find((item) => item.id === id.value)
  if (patient) {
    form.patientNo = patient.patientNo
    form.orderNo = patient.orderNo
    form.name = patient.name
    form.gender = patient.gender
    form.age = patient.age
    form.phone = patient.phone
    form.doctor = patient.doctor
    form.remark = patient.remark
  }
})

function handlePhoneInput(value: string) {
  if (/[^\d]/.test(value)) {
    phoneError.value = '手机号只能输入数字'
    form.phone = value.replace(/\D/g, '').slice(0, 11)
    return
  }
  if (value.length > 11) {
    phoneError.value = '手机号最多 11 位'
    form.phone = value.slice(0, 11)
    return
  }
  phoneError.value = ''
}

function handleAgeInput(value: string | number) {
  const digits = String(value ?? '').replace(/\D/g, '')
  if (!digits) {
    form.age = null
    return
  }
  form.age = Math.min(130, Number(digits))
}

function handleNameInput() {
  if (form.name.trim()) {
    nameError.value = ''
  }
}

function handleGenderChange() {
  if (form.gender) {
    genderError.value = ''
  }
}

function preventReadonlyFocus(event: Event) {
  event.preventDefault()
  const target = event.target as HTMLElement | null
  target?.blur()
}

async function submit() {
  form.name = form.name.trim()
  nameError.value = form.name ? '' : '请输入姓名'
  genderError.value = form.gender ? '' : '请选择性别'
  if (nameError.value || genderError.value) {
    ElMessage.warning('请先填写姓名和性别')
    return
  }
  if (form.phone && !/^1[3-9]\d{9}$/.test(form.phone)) {
    phoneError.value = '请输入有效的 11 位手机号'
    ElMessage.warning(phoneError.value)
    return
  }
  saving.value = true
  try {
    if (id.value) {
      await updatePatient(id.value, form)
      ElMessage.success('患者信息已更新')
      const orderQuery = routeOrderId.value ? `?orderId=${routeOrderId.value}` : ''
      router.push(`/shoot/${id.value}${orderQuery}`)
      return
    }
    const created = await createPatient(form)
    ElMessage.success(`患者 ${created.patientNo} 已创建，订单 ${created.orderNo} 已生成`)
    router.push(`/shoot/${created.id}?orderId=${created.orderId}`)
  } finally {
    saving.value = false
  }
}
</script>

<template>
  <main class="workflow-page">
    <StepHeader active="basic" />
    <FormPanel>
      <header class="form-heading">
        <div>
          <span class="eyebrow">订单建档</span>
          <h1>{{ id ? '编辑患者基本信息' : '新建患者扫描订单' }}</h1>
        </div>
        <span class="status-pill ready">资料待确认</span>
      </header>
      <el-form class="basic-form" label-position="top">
        <el-form-item label="患者编号">
          <el-input class="readonly-control" :model-value="form.patientNo || '保存后自动生成'" readonly tabindex="-1" title="不可编辑" @mousedown.capture="preventReadonlyFocus" @focusin="preventReadonlyFocus" />
        </el-form-item>
        <el-form-item label="订单编号">
          <el-input class="readonly-control" :model-value="form.orderNo || '保存后自动生成'" readonly tabindex="-1" title="不可编辑" @mousedown.capture="preventReadonlyFocus" @focusin="preventReadonlyFocus" />
        </el-form-item>
        <el-form-item label="姓名" required :error="nameError">
          <el-input v-model="form.name" placeholder="患者姓名" @input="handleNameInput" />
        </el-form-item>
        <el-form-item label="年龄">
          <el-input :model-value="form.age ?? ''" class="age-input" inputmode="numeric" placeholder="年龄" @input="handleAgeInput" />
        </el-form-item>
        <el-form-item label="性别" required :error="genderError">
          <el-select v-model="form.gender" placeholder="" @change="handleGenderChange">
            <el-option label="男" value="男" />
            <el-option label="女" value="女" />
          </el-select>
        </el-form-item>
        <el-form-item label="手机号" :error="phoneError">
          <el-input v-model="form.phone" maxlength="11" placeholder="11 位手机号" @input="handlePhoneInput" />
        </el-form-item>
        <el-form-item label="主治医师">
          <el-select v-model="form.doctor" placeholder="">
            <el-option label="李医生" value="李医生" />
            <el-option label="王医生" value="王医生" />
            <el-option label="张医生" value="张医生" />
          </el-select>
        </el-form-item>
        <el-form-item class="wide" label="备注">
          <el-input v-model="form.remark" placeholder="过敏史、拍摄注意事项、交付备注" />
        </el-form-item>
      </el-form>

      <footer class="panel-actions">
        <el-button class="soft-btn form-action-btn" :disabled="saving" @click="router.push('/browse')">
          <X :size="16" />
          <span>取消</span>
        </el-button>
        <el-button class="primary-btn form-action-btn confirm" :loading="saving" @click="submit">
          <Check :size="16" />
          <span>确认并进入拍摄</span>
        </el-button>
      </footer>
    </FormPanel>
  </main>
</template>
