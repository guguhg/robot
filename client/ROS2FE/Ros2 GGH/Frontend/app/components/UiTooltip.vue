<script setup lang="ts">
import {
  TooltipArrow,
  TooltipContent,
  TooltipPortal,
  TooltipProvider,
  TooltipRoot,
  TooltipTrigger,
} from 'reka-ui'

type TooltipSide = 'top' | 'right' | 'bottom' | 'left'

interface Props {
  side?: TooltipSide
  delay?: number
}

const props = withDefaults(defineProps<Props>(), {
  side: 'top',
  delay: 320,
})
</script>

<template>
  <TooltipProvider :delay-duration="props.delay" :skip-delay-duration="180">
    <TooltipRoot :delay-duration="props.delay" :disable-hoverable-content="true">
      <TooltipTrigger as-child>
        <slot name="trigger" />
      </TooltipTrigger>
      <TooltipPortal>
        <TooltipContent class="app-tooltip" :side="props.side" :side-offset="8" :collision-padding="12">
          <slot />
          <TooltipArrow class="app-tooltip-arrow" :width="9" :height="5" />
        </TooltipContent>
      </TooltipPortal>
    </TooltipRoot>
  </TooltipProvider>
</template>

<style>
.app-tooltip {
  z-index: 100;
  max-width: min(240px, calc(100vw - 24px));
  border: 1px solid color-mix(in srgb, var(--foreground) 14%, transparent);
  border-radius: 8px;
  background: color-mix(in srgb, var(--foreground) 94%, #ffffff);
  box-shadow: 0 8px 20px rgba(17, 24, 39, 0.14);
  color: var(--background);
  font-size: 11px;
  line-height: 1.45;
  letter-spacing: 0;
  padding: 7px 9px;
  transform-origin: var(--reka-tooltip-content-transform-origin);
  animation: app-tooltip-in 0.16s ease-out;
}

.app-tooltip[data-state='closed'] { animation: app-tooltip-out 0.1s ease-in; }
.app-tooltip-arrow { fill: color-mix(in srgb, var(--foreground) 94%, #ffffff); }

@keyframes app-tooltip-in {
  from { opacity: 0; transform: translateY(2px) scale(0.98); }
  to { opacity: 1; transform: translateY(0) scale(1); }
}

@keyframes app-tooltip-out {
  to { opacity: 0; transform: translateY(1px) scale(0.99); }
}

@media (hover: none), (pointer: coarse), (prefers-reduced-motion: reduce) {
  .app-tooltip { animation: none; }
}
</style>
