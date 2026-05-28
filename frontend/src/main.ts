import { createApp } from 'vue'
import { createPinia } from 'pinia'
import ElementPlus from 'element-plus'
import 'element-plus/dist/index.css'
import './styles.css'
import App from './App.vue'
import router from './router'

// 挂载 Vue 应用并注册全局插件。
createApp(App).use(createPinia()).use(router).use(ElementPlus).mount('#app')
