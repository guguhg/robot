export default defineNuxtConfig({
  compatibilityDate: '2026-07-21',
  srcDir: '.',
  devtools: { enabled: true },
  css: ['~/assets/css/main.css', '~/assets/css/corporate.css'],
  experimental: {
    viewTransition: true,
  },
  nitro: {
    externals: {
      external: ['estree-walker'],
    },
  },
  hooks: {
    'app:resolve': (app) => {
      // Nuxt 4.5 misreads this internal development diagnostic's re-export as missing a default export.
      app.plugins = app.plugins.filter((plugin) => !plugin.src.includes('check-if-page-unused'))
    },
  },
  app: {
    viewTransition: {
      enabled: true,
    },
    head: {
      htmlAttrs: { lang: 'zh-CN' },
      title: '桥桥友河智能装备',
      meta: [
        {
          name: 'description',
          content: '湖南桥桥友河智能装备有限责任公司，面向工程建设场景提供智能装备、自动化系统集成与安装运维服务。',
        },
        { name: 'theme-color', content: '#132432' },
      ],
      link: [{ rel: 'icon', type: 'image/png', href: '/favicon.png' }],
    },
  },
  vite: {
    optimizeDeps: {
      include: ['three', 'gsap'],
    },
  },
})
