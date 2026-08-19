<script setup lang="ts">
import type { OdometryFrame, Velocity } from '@/lib/cwebapi/cwebapi-client'
import type { LinkState } from '@/composables/useRobot'

const props = withDefaults(defineProps<{
  odometry?: OdometryFrame | null
  safeTwist?: Velocity | null
  state?: LinkState
  error?: string
}>(), {
  odometry: null,
  safeTwist: null,
  state: 'off',
  error: '',
})

function formatValue(value: number | null | undefined, signed = false) {
  if (value === null || value === undefined || !Number.isFinite(value)) return '--'
  const normalized = Math.abs(value) < 0.005 ? 0 : value
  const formatted = normalized.toFixed(2)
  return signed && normalized >= 0 ? `+${formatted}` : formatted
}

const velocity = computed(() => {
  if (props.odometry) {
    return {
      linear: props.odometry.linearVelocity,
      angular: props.odometry.angularVelocity,
      sourceLabel: 'ODO',
      source: '里程计速度',
    }
  }
  if (props.safeTwist) {
    return {
      linear: props.safeTwist.linearX,
      angular: props.safeTwist.angularZ,
      sourceLabel: 'SAFE',
      source: '安全控制输出',
    }
  }
  return { linear: null, angular: null, sourceLabel: 'WAIT', source: '运动遥测' }
})

const cells = computed(() => [
  { k: 'X', v: formatValue(props.odometry?.x, true), available: !!props.odometry, title: '里程计世界坐标 X（米）' },
  { k: 'Y', v: formatValue(props.odometry?.y, true), available: !!props.odometry, title: '里程计世界坐标 Y（米）' },
  { k: 'THETA', v: formatValue(props.odometry?.theta, true), available: !!props.odometry, title: '里程计朝向角 θ（弧度）' },
  { k: 'V', v: formatValue(velocity.value.linear), available: velocity.value.linear !== null, title: `${velocity.value.source}线速度（米/秒）` },
  { k: 'OMEGA', v: formatValue(velocity.value.angular), available: velocity.value.angular !== null, title: `${velocity.value.source}角速度（弧度/秒）` },
])

const telemetryLabel = computed(() => velocity.value.sourceLabel)

const panelTitle = computed(() => {
  if (props.state === 'connecting') return 'MapHub 遥测连接中'
  if (props.state === 'off') return props.error || 'MapHub 遥测未连接'
  if (props.odometry) return props.safeTwist ? '里程计与安全速度遥测已接入' : '里程计遥测已接入'
  if (props.safeTwist) return '已收到安全速度，等待里程计位姿'
  return 'MapHub 已连接，等待运动遥测'
})
</script>

<template>
  <ConsoleHudPanel variant="bare" :label="telemetryLabel" :title="panelTitle">
    <div class="tele-row" :data-state="state">
      <template v-for="(c, i) in cells" :key="c.k">
        <span v-if="i" class="hud-sep" aria-hidden="true"></span>
        <span class="tele-cell" :class="{ 'is-empty': !c.available }" :title="c.title">
          <em class="tele-k" aria-hidden="true">{{ c.k }}</em>
          <span class="tele-value-slot">
            <Transition name="tele-value">
              <b :key="`${c.k}:${c.v}`" class="tele-v hud-mono">{{ c.v }}</b>
            </Transition>
          </span>
        </span>
      </template>
    </div>
  </ConsoleHudPanel>
</template>

<style>
.tele-row {
  display: flex;
  align-items: center;
  gap: 14px;
}

.tele-cell {
  display: inline-flex;
  flex-direction: column;
  align-items: flex-start;
  gap: 3px;
  min-width: 52px;
}

.tele-k {
  font-style: normal;
  font-family: ui-monospace, "Cascadia Mono", Consolas, monospace;
  font-size: 9px;
  line-height: 1;
  letter-spacing: 0;
  color: var(--ornament);
}

.tele-value-slot {
  position: relative;
  display: block;
  width: 7ch;
  height: 13px;
  overflow: hidden;
}

.tele-v {
  display: block;
  width: 100%;
  font-size: 13px;
  font-weight: 500;
  line-height: 1;
  color: var(--foreground);
  font-variant-numeric: tabular-nums;
  white-space: nowrap;
}

.tele-cell.is-empty .tele-v {
  color: var(--muted-foreground);
}

.tele-value-enter-active,
.tele-value-leave-active {
  transition:
    opacity 0.12s ease,
    transform 0.12s ease;
}

.tele-value-leave-active {
  position: absolute;
  inset: 0;
}

.tele-value-enter-from {
  opacity: 0;
  transform: translateY(2px);
}

.tele-value-leave-to {
  opacity: 0;
  transform: translateY(-2px);
}

@media (prefers-reduced-motion: reduce) {
  .tele-value-enter-active,
  .tele-value-leave-active {
    transition: none;
  }
}
</style>
