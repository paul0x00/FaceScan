import { createRouter, createWebHistory } from 'vue-router'
import BrowseView from '../views/BrowseView.vue'
import BasicInfoView from '../views/BasicInfoView.vue'
import LoginView from '../views/LoginView.vue'
import SendView from '../views/SendView.vue'
import SettingsView from '../views/SettingsView.vue'
import ShootView from '../views/ShootView.vue'

/** 应用路由表，覆盖建档、拍摄、点云处理、交付和设置流程。 */
const router = createRouter({
  history: createWebHistory(),
  routes: [
    { path: '/', redirect: '/browse' },
    { path: '/login', component: LoginView },
    { path: '/browse', component: BrowseView },
    { path: '/basic/:id?', component: BasicInfoView },
    { path: '/shoot/:id', component: ShootView },
    { path: '/send/:id', component: SendView },
    { path: '/pointcloud/:id', component: () => import('../views/PointCloudView.vue') },
    { path: '/settings', component: SettingsView }
  ]
})

/** 登录守卫：除登录页外均要求本地 token。 */
router.beforeEach((to) => {
  if (to.path !== '/login' && !localStorage.getItem('facescan.token')) {
    return '/login'
  }
})

export default router
