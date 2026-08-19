<script setup lang="ts">
import type { KeyboardControlState } from '@/composables/useRobot'

const props = defineProps<{
  state: Readonly<KeyboardControlState>
  disabled?: boolean
  estopSignal: number
}>()
</script>

<template>
  <section
    class="keyboard-controls"
    :class="{ 'is-disabled': props.disabled }"
    :aria-label="props.disabled
      ? '键盘控制映射，当前无控制权'
      : '键盘控制映射：W/S 前后，A/D 左右转；W+A、W+D 前旋，S+A、S+D 后旋，Shift 满速，空格急停'"
  >
    <div class="keyboard-head">
      <span class="hud-microlabel" aria-hidden="true">KEYBOARD</span>
      <Icon
        v-if="props.disabled"
        name="lucide:lock-keyhole"
        class="keyboard-lock"
        size="11"
        aria-label="无控制权"
      />
      <span v-else class="keyboard-mode hud-mono" :class="{ 'is-boost': props.state.shift }">
        {{ props.state.shift ? '1.0 M/S' : '0.5 M/S' }}
      </span>
    </div>

    <div class="keyboard-drive" aria-hidden="true">
      <kbd class="keyboard-key keyboard-key-w" :class="{ 'is-active': props.state.w }">W</kbd>
      <kbd class="keyboard-key keyboard-key-a" :class="{ 'is-active': props.state.a }">A</kbd>
      <kbd class="keyboard-key keyboard-key-s" :class="{ 'is-active': props.state.s }">S</kbd>
      <kbd class="keyboard-key keyboard-key-d" :class="{ 'is-active': props.state.d }">D</kbd>
    </div>

    <div class="keyboard-actions" aria-hidden="true">
      <kbd class="keyboard-key keyboard-key-shift" :class="{ 'is-active': props.state.shift }">SHIFT</kbd>
      <kbd class="keyboard-key keyboard-key-space" :class="{ 'is-active': props.state.space }">
        SPACE
        <span v-if="props.estopSignal > 0" :key="props.estopSignal" class="keyboard-stop-pulse"></span>
      </kbd>
    </div>
  </section>
</template>

<style>
.keyboard-controls {
  width: 148px;
  color: var(--foreground);
  user-select: none;
}

.keyboard-head {
  display: flex;
  align-items: center;
  justify-content: space-between;
  height: 16px;
  margin-bottom: 8px;
}

.keyboard-head .hud-microlabel {
  margin: 0;
}

.keyboard-mode {
  font-size: 8.5px;
  color: var(--muted-foreground);
  transition: color 0.16s ease;
}

.keyboard-mode.is-boost {
  color: var(--primary);
}

.keyboard-lock {
  color: var(--muted-foreground);
}

.keyboard-drive {
  display: grid;
  grid-template-areas:
    '. w .'
    'a s d';
  grid-template-columns: repeat(3, 44px);
  grid-template-rows: repeat(2, 36px);
  gap: 6px 8px;
}

.keyboard-key-w { grid-area: w; }
.keyboard-key-a { grid-area: a; }
.keyboard-key-s { grid-area: s; }
.keyboard-key-d { grid-area: d; }

.keyboard-actions {
  display: grid;
  grid-template-columns: 62px 78px;
  gap: 8px;
  margin-top: 8px;
}

.keyboard-key {
  position: relative;
  display: flex;
  align-items: center;
  justify-content: center;
  box-sizing: border-box;
  width: 44px;
  height: 36px;
  overflow: visible;
  border: 1px solid var(--glass-border);
  border-radius: 6px;
  background: var(--glass-bg-strong);
  color: var(--foreground);
  font-family: ui-monospace, "Cascadia Mono", Consolas, monospace;
  font-size: 11px;
  font-weight: 600;
  line-height: 1;
  letter-spacing: 0;
  box-shadow:
    0 3px 0 color-mix(in srgb, var(--border) 72%, var(--foreground) 28%),
    0 7px 14px rgba(17, 24, 39, 0.08);
  transform: translateY(0);
  transition:
    color 0.12s ease,
    border-color 0.12s ease,
    background-color 0.12s ease,
    box-shadow 0.12s ease,
    transform 0.12s ease,
    opacity 0.25s ease;
}

.keyboard-key::before {
  content: '';
  position: absolute;
  inset: 3px 4px auto;
  height: 1px;
  border-radius: 1px;
  background: currentColor;
  opacity: 0.12;
}

.keyboard-key.is-active {
  border-color: color-mix(in srgb, var(--primary) 70%, transparent);
  background: color-mix(in srgb, var(--primary) 16%, var(--glass-bg-strong));
  color: var(--primary);
  box-shadow:
    0 1px 0 color-mix(in srgb, var(--primary) 35%, var(--border)),
    0 3px 8px rgba(37, 99, 235, 0.18);
  transform: translateY(3px);
}

.keyboard-key-shift,
.keyboard-key-space {
  width: 100%;
  font-size: 9px;
}

.keyboard-key-space.is-active {
  border-color: rgba(220, 38, 38, 0.7);
  background: rgba(220, 38, 38, 0.14);
  color: #dc2626;
  box-shadow:
    0 1px 0 rgba(220, 38, 38, 0.35),
    0 3px 8px rgba(220, 38, 38, 0.2);
}

.keyboard-stop-pulse {
  position: absolute;
  inset: -4px;
  border: 1px solid rgba(220, 38, 38, 0.72);
  border-radius: 8px;
  pointer-events: none;
  animation: keyboard-stop-pulse 0.46s ease-out both;
}

.keyboard-controls.is-disabled .keyboard-key {
  filter: grayscale(0.7);
  opacity: 0.42;
  box-shadow: 0 2px 0 var(--border);
}

@keyframes keyboard-stop-pulse {
  from {
    opacity: 0.8;
    transform: scale(0.94);
  }
  to {
    opacity: 0;
    transform: scale(1.12);
  }
}

@media (prefers-reduced-motion: reduce) {
  .keyboard-mode,
  .keyboard-key {
    transition: none;
  }

  .keyboard-stop-pulse {
    animation: none;
    opacity: 0;
  }
}
</style>
