<script setup lang="ts">
import { Motion } from 'motion-v'

interface StageOpt {
  key: string
  label: string
  icon: string
}

const props = defineProps<{
  stages: StageOpt[]
  modelValue: string
}>()

const emit = defineEmits<{ (e: 'update:modelValue', v: string): void }>()

const ITEM_H = 34

const idx = computed(() =>
  Math.max(0, props.stages.findIndex(s => s.key === props.modelValue)),
)
</script>

<template>
  <ConsoleHudPanel variant="bare" label="VIEW">
    <div class="switcher" role="tablist" aria-label="主舞台视图切换">
      <!-- 词汇表 J′：清单键线 -->
      <Motion
        class="switcher-keyline"
        aria-hidden="true"
        :animate="{ y: idx * ITEM_H + 5 }"
        :transition="{ type: 'spring', stiffness: 420, damping: 34 }"
      />
      <button
        v-for="s in stages"
        :key="s.key"
        role="tab"
        class="switcher-item"
        :aria-selected="s.key === modelValue"
        :class="{ active: s.key === modelValue }"
        @click="emit('update:modelValue', s.key)"
      >
        <Icon :name="s.icon" size="15" />
        <span>{{ s.label }}</span>
      </button>
    </div>
  </ConsoleHudPanel>
</template>

<style>
.switcher {
  position: relative;
  display: flex;
  flex-direction: column;
  width: 88px;
}

.switcher-keyline {
  position: absolute;
  left: 0;
  top: 0;
  width: 2px;
  height: 24px;
  border-radius: 1px;
  background: var(--primary);
}

.switcher-item {
  position: relative;
  display: flex;
  align-items: center;
  gap: 8px;
  height: 34px;
  padding: 0 0 0 14px;
  border: 0;
  background: transparent;
  color: var(--muted-foreground);
  font-size: 13px;
  cursor: pointer;
  transition: color 0.15s ease;
}

.switcher-item:hover {
  color: var(--foreground);
}

.switcher-item.active {
  color: var(--foreground);
  font-weight: 600;
}
</style>
