<script setup lang="ts">
const requestEvent = useRequestEvent()

if (requestEvent) setResponseStatus(requestEvent, 404)

useHead({
  title: '404｜桥桥友河智能装备',
  meta: [
    {
      name: 'robots',
      content: 'noindex',
    },
  ],
})
</script>

<template>
  <RouteRecovery :status-code="404" />
</template>
