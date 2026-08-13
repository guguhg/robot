<script setup lang="ts">
import { company, siteNavigation } from '~/data/site-content'

const route = useRoute()
const menuOpen = ref(false)
const usesLightHeader = computed(() => route.path === '/about')

watch(
  () => route.fullPath,
  () => {
    menuOpen.value = false
  },
)

const isActive = (path: string) => {
  if (path === '/') return route.path === '/'
  return route.path.startsWith(path)
}
</script>

<template>
  <header
    class="corp-header"
    :class="{ 'corp-header--light': usesLightHeader, 'is-open': menuOpen }"
  >
    <div class="corp-header__inner">
      <NuxtLink class="corp-brand" to="/" aria-label="返回桥桥友河智能装备首页">
        <span class="corp-brand__mark" aria-hidden="true">
          <img src="/media/brand/QQYH.jpg" alt="" width="36" height="36">
        </span>
        <span class="corp-brand__copy">
          <strong>桥桥友河</strong>
          <small>{{ company.englishName }}</small>
        </span>
      </NuxtLink>

      <button
        class="corp-menu-button"
        type="button"
        :aria-expanded="menuOpen"
        aria-controls="corp-navigation"
        aria-label="切换网站导航"
        @click="menuOpen = !menuOpen"
      >
        <span />
        <span />
      </button>

      <nav id="corp-navigation" class="corp-navigation" aria-label="主要导航">
        <NuxtLink
          v-for="item in siteNavigation"
          :key="item.to"
          :to="item.to"
          :class="{ 'is-active': isActive(item.to) }"
        >
          {{ item.label }}
        </NuxtLink>
      </nav>
    </div>
  </header>
</template>
