<script setup lang="ts">
import { computed, onMounted, reactive } from 'vue'
import { useRoute, useRouter } from 'vue-router'
import { ElMessage } from 'element-plus'
import FormPanel from '../components/FormPanel.vue'
import StepHeader from '../components/StepHeader.vue'
import { createPatient, updatePatient } from '../api/client'
import { usePatientStore } from '../stores/patients'
import type { PatientForm } from '../types'

const route = useRoute()
const router = useRouter()
const store = usePatientStore()
const id = computed(() => Number(route.params.id || 0))

const form = reactive<PatientForm>({
  patientNo: '',
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
    form.name = patient.name
    form.gender = patient.gender
    form.age = patient.age
    form.phone = patient.phone
    form.doctor = patient.doctor
    form.remark = patient.remark
  }
})

async function submit() {
  if (id.value) {
    await updatePatient(id.value, form)
    ElMessage.success('患者信息已更新')
    router.push(`/shoot/${id.value}`)
    return
  }
  const patientId = await createPatient(form)
  ElMessage.success('患者已创建')
  router.push(`/shoot/${patientId}`)
}
</script>

<template>
  <main class="workflow-page">
    <StepHeader active="basic" />
    <FormPanel>
      <el-form class="basic-form" label-position="top">
        <el-form-item label="患者编号">
          <el-input v-model="form.patientNo" />
        </el-form-item>
        <el-form-item label="姓名">
          <el-input v-model="form.name" />
        </el-form-item>
        <el-form-item label="年龄">
          <el-input-number v-model="form.age" :min="0" :max="130" controls-position="right" />
        </el-form-item>
        <el-form-item label="性别">
          <el-select v-model="form.gender" placeholder="">
            <el-option label="男" value="男" />
            <el-option label="女" value="女" />
          </el-select>
        </el-form-item>
        <el-form-item label="联系方式">
          <el-input v-model="form.phone" />
        </el-form-item>
        <el-form-item label="主治医师">
          <el-select v-model="form.doctor" placeholder="">
            <el-option label="李医生" value="李医生" />
            <el-option label="王医生" value="王医生" />
            <el-option label="张医生" value="张医生" />
          </el-select>
        </el-form-item>
        <el-form-item class="wide" label="备注">
          <el-input v-model="form.remark" />
        </el-form-item>
      </el-form>

      <footer class="panel-actions">
        <el-button class="strong-btn" @click="submit">确认</el-button>
        <el-button class="strong-btn" @click="router.push('/browse')">取消</el-button>
      </footer>
    </FormPanel>
  </main>
</template>

