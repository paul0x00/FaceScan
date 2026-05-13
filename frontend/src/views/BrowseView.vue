<script setup lang="ts">
import { computed, onMounted, reactive, ref } from 'vue'
import { useRouter } from 'vue-router'
import { CirclePlus, Edit, FileText } from 'lucide-vue-next'
import { usePatientStore } from '../stores/patients'
import LogoMark from '../components/LogoMark.vue'
import WindowIcons from '../components/WindowIcons.vue'

const router = useRouter()
const store = usePatientStore()
const filters = reactive({
  keyword: '',
  date: ''
})
const activeId = ref(0)

const selected = computed(() => store.patients.find((item) => item.id === activeId.value) ?? store.patients[0])

onMounted(async () => {
  await store.load()
  activeId.value = store.selected?.id ?? 0
})

async function search() {
  await store.load(filters)
  activeId.value = store.patients[0]?.id ?? 0
}

function reset() {
  filters.keyword = ''
  filters.date = ''
  search()
}

function openBasic(id?: number) {
  router.push(id ? `/basic/${id}` : '/basic')
}

function openShoot(id: number) {
  router.push(`/shoot/${id}`)
}
</script>

<template>
  <main class="browse-page">
    <header class="browse-titlebar">
      <LogoMark />
      <WindowIcons />
    </header>

    <section class="browse-toolbar">
      <el-input v-model="filters.keyword" class="keyword-input" placeholder="关键字搜索栏" clearable />
      <el-date-picker v-model="filters.date" class="date-input" type="date" value-format="YYYY-MM-DD" placeholder="选择时间" />
      <el-button class="strong-btn" @click="search">搜索</el-button>
      <el-button class="strong-btn" @click="reset">重置</el-button>
      <el-button class="strong-btn new-patient" @click="openBasic()">新建患者</el-button>
    </section>

    <section class="browse-content">
      <aside class="patient-list" v-loading="store.loading">
        <button
          v-for="patient in store.patients"
          :key="patient.id"
          class="patient-row"
          :class="{ active: activeId === patient.id }"
          type="button"
          @click="activeId = patient.id"
        >
          <div class="thumb">面扫截图</div>
          <div class="patient-summary">
            <strong>{{ patient.name || '姓名' }}，{{ patient.gender || '性别' }}</strong>
            <span>{{ patient.patientNo || '患者编号' }}</span>
            <span>{{ patient.createdAt || '创建时间' }}</span>
          </div>
          <CirclePlus class="row-add" :size="30" @click.stop="openShoot(patient.id)" />
        </button>
      </aside>

      <section class="scan-workspace">
        <template v-if="selected">
          <article v-for="index in 2" :key="index" class="scan-card">
            <header>
              <span>{{ selected.createdAt || '创建时间' }}</span>
              <div class="scan-actions">
                <FileText :size="22" />
                <Edit :size="24" @click="openBasic(selected.id)" />
              </div>
            </header>
            <button class="scan-preview" type="button" @click="openShoot(selected.id)">面扫截图</button>
          </article>
        </template>
      </section>
    </section>
  </main>
</template>
