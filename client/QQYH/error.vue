<script setup lang="ts">
interface RouteError {
  statusCode?: number
}

const props = defineProps<{
  error: RouteError
}>()

const statusCode = computed(() => props.error?.statusCode ?? 500)

useHead({
  title: `${statusCode.value}｜桥桥友河智能装备`,
  meta: [
    {
      name: 'robots',
      content: 'noindex',
    },
  ],
})
</script>

<template>
  <RouteRecovery :status-code="statusCode" />
</template>
