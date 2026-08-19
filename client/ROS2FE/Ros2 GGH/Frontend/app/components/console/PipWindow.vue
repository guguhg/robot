<script setup lang="ts">
interface StageOpt {
  key: string
  label: string
  icon: string
}

defineProps<{ stage: StageOpt }>()

const emit = defineEmits<{ (e: 'swap'): void }>()
</script>

<template>
  <ConsoleHudPanel class="pip-panel" :label="stage.key.toUpperCase()" variant="bare">
    <button
      class="pip-body"
      :aria-label="`与主舞台互换（小窗当前：${stage.label}）`"
      :title="`与主舞台互换（当前：${stage.label}）`"
      @click="emit('swap')"
    >
      <span class="pip-grid" aria-hidden="true"></span>
      <span class="pip-corners" aria-hidden="true"><i /><i /><i /><i /></span>
      <Transition name="pipfade" mode="out-in">
        <span :key="stage.key" class="pip-content">
          <Icon :name="stage.icon" size="18" />
        </span>
      </Transition>
      <span class="pip-swap" aria-hidden="true">
        <Icon name="lucide:repeat-2" size="12" />
      </span>
    </button>
  </ConsoleHudPanel>
</template>

<style>
.pip-panel {
  width: 184px;
}

.pip-panel > .hud-microlabel {
  margin: 0 0 8px 2px;
}

.pip-panel > .hud-brackets {
  display: none;
}

.pip-body {
  position: relative;
  display: flex;
  align-items: center;
  justify-content: center;
  width: 184px;
  height: 104px;
  overflow: hidden;
  border: 0;
  border-radius: 0;
  background: transparent;
  color: var(--muted-foreground);
  cursor: pointer;
  transition:
    background-color 0.18s ease,
    color 0.18s ease;
}

.pip-body:hover,
.pip-body:focus-visible {
  background: color-mix(in srgb, var(--primary) 4%, transparent);
  color: var(--foreground);
}

.pip-grid {
  position: absolute;
  inset: 8px;
  background-image: radial-gradient(var(--ornament-faint) 1px, transparent 1px);
  background-size: 20px 20px;
  opacity: 0.72;
  transition: opacity 0.18s ease;
}

.pip-body:hover .pip-grid,
.pip-body:focus-visible .pip-grid {
  opacity: 1;
}

.pip-corners {
  position: absolute;
  inset: 0;
  pointer-events: none;
}

.pip-corners i {
  position: absolute;
  width: 16px;
  height: 16px;
  border: 0 solid var(--ornament);
  opacity: 0.9;
  transition:
    width 0.18s ease,
    height 0.18s ease,
    border-color 0.18s ease,
    opacity 0.18s ease;
}

.pip-corners i:nth-child(1) { top: 0; left: 0; border-top-width: 1px; border-left-width: 1px; }
.pip-corners i:nth-child(2) { top: 0; right: 0; border-top-width: 1px; border-right-width: 1px; }
.pip-corners i:nth-child(3) { right: 0; bottom: 0; border-right-width: 1px; border-bottom-width: 1px; }
.pip-corners i:nth-child(4) { bottom: 0; left: 0; border-bottom-width: 1px; border-left-width: 1px; }

.pip-body:hover .pip-corners i,
.pip-body:focus-visible .pip-corners i {
  width: 22px;
  height: 22px;
  border-color: var(--primary);
  opacity: 1;
}

.pip-content {
  position: relative;
  z-index: 1;
  display: flex;
  transition: transform 0.18s ease;
}

.pip-body:hover .pip-content,
.pip-body:focus-visible .pip-content {
  transform: scale(1.08);
}

.pip-swap {
  position: absolute;
  right: 8px;
  bottom: 8px;
  z-index: 1;
  display: flex;
  color: var(--primary);
  opacity: 0;
  transform: translateX(4px);
  transition:
    opacity 0.15s ease,
    transform 0.18s ease;
}

.pip-body:hover .pip-swap,
.pip-body:focus-visible .pip-swap {
  opacity: 1;
  transform: translateX(0);
}

.pipfade-enter-active,
.pipfade-leave-active {
  transition: opacity 0.15s ease;
}

.pipfade-enter-from,
.pipfade-leave-to {
  opacity: 0;
}

@media (prefers-reduced-motion: reduce) {
  .pip-body,
  .pip-grid,
  .pip-corners i,
  .pip-content,
  .pip-swap {
    transition: none;
  }
}
</style>
