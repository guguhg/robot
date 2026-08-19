<script setup lang="ts">
import { gsap } from 'gsap'

const props = defineProps<{
  /* 外部驱动矢量（键盘驾驶时旋钮映像），拖拽期间忽略 */
  driven?: { x: number; y: number; m: number }
  disabled?: boolean
}>()

const emit = defineEmits<{
  (e: 'move', value: { x: number; y: number; force: number }): void
  (e: 'release'): void
}>()

const padEl = ref<HTMLElement | null>(null)
const knobEl = ref<HTMLElement | null>(null)
const force = ref(0)

/* 摇杆几何：最大矢量半径 / 旋钮视觉行程 */
const RADIUS = 55
const KNOB_TRAVEL = 28

let dragging = false

function apply(e: PointerEvent) {
  if (!padEl.value || !knobEl.value) return
  const r = padEl.value.getBoundingClientRect()
  let dx = e.clientX - (r.left + r.width / 2)
  let dy = e.clientY - (r.top + r.height / 2)
  const len = Math.hypot(dx, dy)
  const f = Math.min(1, len / RADIUS)
  if (len > RADIUS) {
    dx = (dx / len) * RADIUS
    dy = (dy / len) * RADIUS
  }
  force.value = f
  emit('move', { x: dx / RADIUS, y: dy / RADIUS, force: f })
  gsap.set(knobEl.value, {
    x: dx * (KNOB_TRAVEL / RADIUS),
    y: dy * (KNOB_TRAVEL / RADIUS),
  })
}

function onDown(e: PointerEvent) {
  if (props.disabled) return
  dragging = true
  try {
    padEl.value?.setPointerCapture(e.pointerId)
  } catch {
    /* 合成事件的 pointerId 可能无效，忽略 */
  }
  apply(e)
}

function onMove(e: PointerEvent) {
  if (dragging) apply(e)
}

watch(
  () => props.driven,
  (v) => {
    if (!v || dragging) return
    force.value = v.m
    if (knobEl.value) {
      gsap.to(knobEl.value, {
        x: v.x * KNOB_TRAVEL,
        y: v.y * KNOB_TRAVEL,
        duration: 0.18,
        ease: 'power2.out',
        overwrite: 'auto',
      })
    }
  },
)

watch(
  () => props.disabled,
  (disabled) => {
    if (!disabled) return
    dragging = false
    force.value = 0
    emit('release')
    if (knobEl.value) gsap.to(knobEl.value, { x: 0, y: 0, duration: 0.18, ease: 'power2.out' })
  },
)

function onUp(e: PointerEvent) {
  if (!dragging) return
  dragging = false
  try {
    padEl.value?.releasePointerCapture(e.pointerId)
  } catch {
    /* 同上 */
  }
  force.value = 0
  emit('release')
  if (knobEl.value) {
    gsap.to(knobEl.value, { x: 0, y: 0, duration: 0.5, ease: 'elastic.out(1, 0.45)' })
  }
}
</script>

<template>
  <!-- 玻璃预算：摇杆盘不用 backdrop-filter -->
  <div
    ref="padEl"
    class="joy-pad"
    :class="{ 'is-disabled': props.disabled }"
    :style="{ '--force': force }"
    aria-label="虚拟摇杆"
    :aria-disabled="props.disabled"
    @pointerdown="onDown"
    @pointermove="onMove"
    @pointerup="onUp"
    @pointercancel="onUp"
  >
    <svg class="joy-ticks" viewBox="0 0 120 120" aria-hidden="true">
      <line
        v-for="(t, i) in Array.from({ length: 12 }, (_, n) => {
          const a = (n * 30 * Math.PI) / 180
          return {
            x1: 60 + 52 * Math.cos(a),
            y1: 60 + 52 * Math.sin(a),
            x2: 60 + 57 * Math.cos(a),
            y2: 60 + 57 * Math.sin(a),
          }
        })"
        :key="i"
        :x1="t.x1"
        :y1="t.y1"
        :x2="t.x2"
        :y2="t.y2"
        stroke="currentColor"
        stroke-width="1"
      />
    </svg>
    <div ref="knobEl" class="joy-knob"></div>
  </div>
</template>

<style>
.joy-pad {
  position: relative;
  width: 120px;
  height: 120px;
  border-radius: 50%;
  background: rgba(255, 255, 255, 0.72);
  border: 1px solid var(--glass-border);
  touch-action: none;
  cursor: grab;
}

.joy-pad:active {
  cursor: grabbing;
}

.joy-pad.is-disabled {
  cursor: not-allowed;
  opacity: 0.42;
}

.joy-pad.is-disabled:active {
  cursor: not-allowed;
}

/* 词汇表 C 联动：刻度环随力度渐显 */
.joy-ticks {
  position: absolute;
  inset: 0;
  color: var(--ornament);
  opacity: calc(0.35 + var(--force, 0) * 0.65);
  transition: opacity 0.1s linear;
  pointer-events: none;
}

/* 力度环：强度越大主色描边越实 */
.joy-pad::after {
  content: '';
  position: absolute;
  inset: -1px;
  border-radius: 50%;
  border: 1.5px solid var(--primary);
  opacity: calc(var(--force, 0) * 0.55);
  transition: opacity 0.1s linear;
  pointer-events: none;
}

.joy-knob {
  position: absolute;
  left: 50%;
  top: 50%;
  width: 44px;
  height: 44px;
  margin: -22px 0 0 -22px;
  border-radius: 50%;
  background: #fff;
  border: 1px solid var(--glass-border);
  box-shadow: 0 2px 8px rgba(17, 24, 39, 0.08);
  pointer-events: none;
}
</style>
