import tailwindcss from '@tailwindcss/vite'

const backendBase = (process.env.NUXT_BACKEND_BASE_URL || 'http://192.168.1.139:5122').replace(/\/$/, '')
const srsBaseUrl = (process.env.NUXT_SRS_BASE_URL || '').replace(/\/$/, '')
const rosbridgeCameraUrl = (process.env.NUXT_ROSBRIDGE_CAMERA_URL || 'ws://192.168.1.30:9091').replace(/\/$/, '')
const rosbridgeCameraTopic = process.env.NUXT_ROSBRIDGE_CAMERA_TOPIC || '/aurora/rgb/image_raw'

export default defineNuxtConfig({
  compatibilityDate: '2026-08-02',
  modules: ['shadcn-nuxt', '@nuxt/icon'],
  icon: {
    clientBundle: {
      /* 首屏与动态操作者按钮需避开 SSR 阶段的运行时回退请求。 */
      icons: [
        'lucide:eye',
        'lucide:eye-off',
        'lucide:log-in',
        'lucide:user-round-cog',
        'lucide:refresh-cw',
        'lucide:lock-keyhole',
        'lucide:users-round',
        'lucide:rotate-ccw',
        'lucide:arrow-right-left',
        'lucide:check',
        'lucide:arrow-up-right',
        'lucide:battery-medium',
        'lucide:box',
        'lucide:gamepad-2',
        'lucide:info',
        'lucide:map',
        'lucide:monitor-up',
        'lucide:radio-tower',
        'lucide:shield-check',
        'lucide:user-round',
        'lucide:video',
      ],
      scan: true,
    },
  },
  css: ['~/assets/css/main.css'],
  vite: {
    plugins: [tailwindcss()],
  },
  devServer: {
    host: 'localhost',
    port: 5173,
  },
  shadcn: {
    prefix: '',
    componentDir: '~/components/ui',
  },
  runtimeConfig: {
    public: {
      /* 直连后端；开发站点固定 localhost:5173 以匹配后端 CORS 白名单 */
      apiBase: backendBase,
      /* 可选；未配置时仅将后端签发的 localhost SRS 主机替换为 apiBase 主机。 */
      srsBaseUrl,
      rosbridgeCameraUrl,
      rosbridgeCameraTopic,
    },
  },
  app: {
    head: {
      htmlAttrs: { lang: 'zh-CN' },
      title: 'ZeroRobot',
      meta: [
        { name: 'viewport', content: 'width=device-width, initial-scale=1' },
        { name: 'description', content: 'ROS2 机器人远程操作控制台' },
      ],
    },
  },
})
