<script setup lang="ts">
type Chamfer = 'tl' | 'tr' | 'bl' | 'br' | 'none'

const props = withDefaults(
  defineProps<{
    label?: string
    chamfer?: Chamfer
    variant?: 'glass' | 'bare'
  }>(),
  { chamfer: 'br', variant: 'glass' },
)

const polys: Record<Chamfer, string> = {
  br: 'polygon(0 0, 100% 0, 100% calc(100% - var(--chamfer)), calc(100% - var(--chamfer)) 100%, 0 100%)',
  bl: 'polygon(0 0, 100% 0, 100% 100%, var(--chamfer) 100%, 0 calc(100% - var(--chamfer)))',
  tr: 'polygon(0 0, calc(100% - var(--chamfer)) 0, 100% var(--chamfer), 100% 100%, 0 100%)',
  tl: 'polygon(var(--chamfer) 0, 100% 0, 100% 100%, 0 100%, 0 var(--chamfer))',
  none: 'none',
}
</script>

<template>
  <div
    class="hud-panel"
    :class="[variant === 'bare' ? 'bare' : 'glass', { 'no-label': !label }]"
    :style="variant === 'glass' ? { '--panel-clip': polys[props.chamfer] } : undefined"
  >
    <template v-if="variant === 'glass'">
      <div class="hud-panel-bd">
        <div class="hud-panel-face">
          <span v-if="label" class="hud-microlabel" aria-hidden="true">{{ label }}</span>
          <slot />
        </div>
      </div>
    </template>

    <template v-else>
      <span v-if="label" class="hud-microlabel" aria-hidden="true">{{ label }}</span>
      <slot />
    </template>

    <span class="hud-brackets" aria-hidden="true"><i /><i /><i /><i /></span>
  </div>
</template>
