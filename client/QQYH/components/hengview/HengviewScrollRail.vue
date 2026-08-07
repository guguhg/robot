<script setup lang="ts">
import { computed } from 'vue'
import { HENGVIEW_SECTIONS } from '~/lib/hengview/hengview-scene'

const props = defineProps<{
  progress: number
  active: boolean
}>()

const normalized = computed(() => Math.min(1, Math.max(0, props.progress)))
const fillStyle = computed(() => ({
  transform: `scaleY(${Math.max(normalized.value, 0.015)})`,
}))
const ticks = computed(() => HENGVIEW_SECTIONS.map((at) => ({ at, passed: normalized.value >= at })))
</script>

<template>
  <div class="cl-rail" :class="{ 'is-active': active }" aria-hidden="true">
    <span class="cl-rail__hintLabel">SCROLL</span>
    <span class="cl-rail__track">
      <i class="cl-rail__drift" />
      <i class="cl-rail__fill" :style="fillStyle" />
      <i
        v-for="tick in ticks"
        :key="tick.at"
        class="cl-rail__tick"
        :class="{ 'is-passed': tick.passed }"
        :style="{ top: `${tick.at * 100}%` }"
      />
    </span>
  </div>
</template>

<style scoped>
.cl-rail {
  position: absolute;
  bottom: 34px;
  left: 50%;
  z-index: 5;
  display: grid;
  gap: 10px;
  justify-items: center;
  pointer-events: none;
  transform: translateX(-50%);
  transition: right 360ms ease, bottom 360ms ease, left 360ms ease, transform 360ms ease;
}

.cl-rail__hintLabel {
  color: color-mix(in srgb, var(--mist, #8A97A6) 55%, transparent);
  font-family: 'Chakra Petch', 'IBM Plex Mono', ui-monospace, monospace;
  font-size: 9px;
  font-weight: 500;
  letter-spacing: 0.34em;
  transition: opacity 300ms ease, color 750ms ease;
}

.cl-rail__track {
  position: relative;
  display: block;
  width: 1px;
  height: 64px;
  overflow: visible;
  background: var(--line, rgb(198 216 228 / 12%));
  transition: background-color 750ms ease;
}

.cl-rail__drift {
  position: absolute;
  top: 0;
  left: 0;
  width: 1px;
  height: 18px;
  background: var(--cyan, #46D7EA);
  box-shadow: 0 0 8px color-mix(in srgb, var(--cyan, #46D7EA) 55%, transparent);
  animation: cl-rail-drift 1.9s cubic-bezier(0.45, 0, 0.55, 1) infinite;
}

.cl-rail__fill {
  position: absolute;
  top: 0;
  left: 0;
  width: 1px;
  height: 100%;
  background: linear-gradient(
    var(--cyan, #46D7EA),
    color-mix(in srgb, var(--cyan, #46D7EA) 45%, transparent)
  );
  opacity: 0;
  transform-origin: top;
  transition: opacity 300ms ease;
}

.cl-rail__tick {
  position: absolute;
  left: -2px;
  width: 5px;
  height: 1px;
  background: var(--line-strong, rgb(198 216 228 / 22%));
  opacity: 0;
  transition: background-color 300ms ease, opacity 300ms ease;
}

.cl-rail__tick.is-passed {
  background: color-mix(in srgb, var(--cyan, #46D7EA) 75%, transparent);
}

.cl-rail.is-active {
  right: 30px;
  bottom: 50%;
  left: auto;
  transform: translateY(50%);
}

.cl-rail.is-active .cl-rail__hintLabel {
  opacity: 0;
}

.cl-rail.is-active .cl-rail__track {
  height: min(30svh, 260px);
}

.cl-rail.is-active .cl-rail__drift {
  animation: none;
  opacity: 0;
}

.cl-rail.is-active .cl-rail__fill,
.cl-rail.is-active .cl-rail__tick {
  opacity: 1;
}

@keyframes cl-rail-drift {
  0% {
    top: 0;
    opacity: 0;
  }

  18% {
    opacity: 1;
  }

  82% {
    opacity: 1;
  }

  100% {
    top: calc(100% - 18px);
    opacity: 0;
  }
}

@media (max-width: 700px) {
  .cl-rail {
    bottom: 24px;
  }

  .cl-rail.is-active {
    right: 16px;
  }

  .cl-rail.is-active .cl-rail__track {
    height: 22svh;
  }
}

@media (prefers-reduced-motion: reduce) {
  .cl-rail,
  .cl-rail__drift {
    animation: none;
    transition: none;
  }

  .cl-rail__drift {
    top: 40%;
    opacity: 0.7;
  }
}
</style>
