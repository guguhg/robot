<script setup lang="ts">
import { gsap } from 'gsap'
import { Motion } from 'motion-v'

const emit = defineEmits<{ (e: 'stop'): void }>()
const props = defineProps<{ disabled?: boolean }>()

const ringEl = ref<HTMLElement | null>(null)
const btnEl = ref<HTMLElement | null>(null)

/* 纯视觉反馈：供点击与外部（Space 急停）共用 */
function flash() {
  if (props.disabled) return
  if (window.matchMedia('(prefers-reduced-motion: reduce)').matches) return
  if (!ringEl.value || !btnEl.value) return

  gsap
    .timeline()
    /* 按压回弹作用在外层 wrap，避免与 motion-v 的按压 scale 抢 transform */
    .fromTo(btnEl.value, { scale: 1 }, { scale: 0.94, duration: 0.09, yoyo: true, repeat: 1, ease: 'power2.out', clearProps: 'transform' }, 0)
    .fromTo(ringEl.value, { rotate: 0 }, { rotate: 360, duration: 0.6, ease: 'power2.out' }, 0)
    .set(ringEl.value, { clearProps: 'transform' })
    .fromTo(
      btnEl.value.querySelector('.estop'),
      { boxShadow: '0 4px 14px rgba(220, 38, 38, 0.28)' },
      {
        boxShadow: '0 0 0 10px rgba(220, 38, 38, 0.22)',
        duration: 0.16,
        yoyo: true,
        repeat: 3,
        ease: 'power1.inOut',
        clearProps: 'boxShadow',
      },
      0,
    )
}

function trigger() {
  if (props.disabled) return
  flash()
  emit('stop')
}

defineExpose({ flash })
</script>

<template>
  <div ref="btnEl" class="estop-wrap">
    <!-- 词汇表 I：警示斜纹环，安全语义专用 -->
    <div ref="ringEl" class="estop-ring">
      <Motion
        as="button"
        class="estop"
        aria-label="急停"
        :disabled="props.disabled"
        :while-hover="props.disabled ? undefined : { scale: 1.04 }"
        :while-press="props.disabled ? undefined : { scale: 0.94 }"
        :transition="{ type: 'spring', stiffness: 500, damping: 30 }"
        @click="trigger"
      >
        STOP
      </Motion>
    </div>
  </div>
</template>

<style>
.estop-ring {
  padding: 6px;
  border-radius: 50%;
  border: 1px solid rgba(220, 38, 38, 0.25);
  background: repeating-linear-gradient(
    45deg,
    rgba(220, 38, 38, 0.14) 0 4px,
    transparent 4px 8px
  );
}

.estop {
  display: flex;
  align-items: center;
  justify-content: center;
  width: 58px;
  height: 58px;
  border: 0;
  border-radius: 50%;
  background: #dc2626;
  color: #fff;
  font-family: ui-monospace, "Cascadia Mono", Consolas, monospace;
  font-size: 11px;
  font-weight: 600;
  letter-spacing: 0.12em;
  cursor: pointer;
  box-shadow: 0 4px 14px rgba(220, 38, 38, 0.28);
}

.estop:disabled {
  cursor: not-allowed;
  filter: grayscale(0.65);
  opacity: 0.42;
  box-shadow: none;
}
</style>
