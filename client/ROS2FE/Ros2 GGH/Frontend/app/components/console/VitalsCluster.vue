<script setup lang="ts">
import type { LinkState } from '@/composables/useRobot'

interface LinkInfo {
  key: string
  state: LinkState
  title: string
}

const props = withDefaults(
  defineProps<{
    batteryPercent?: number | null
    batteryVoltage?: number | null
    batteryState?: LinkState
    batteryError?: string
    speedCurrent?: number
    speedLimit?: number
    username?: string
    links?: LinkInfo[]
  }>(),
  {
    batteryPercent: null,
    batteryVoltage: null,
    batteryState: 'off',
    batteryError: '',
    speedCurrent: 0,
    speedLimit: 1,
    username: 'OFFLINE',
    links: () => [
      { key: 'API', state: 'off', title: '后端 API：未连接' },
      { key: 'ROS', state: 'off', title: '控制通道：未连接' },
      { key: 'VID', state: 'off', title: '视频流：未接入' },
    ],
  },
)

const ticks = Array.from({ length: 9 }, (_, i) => (i + 1) * 10)

const normalizedPercent = computed(() => {
  if (typeof props.batteryPercent !== 'number' || !Number.isFinite(props.batteryPercent)) return null
  return Math.min(100, Math.max(0, props.batteryPercent))
})
const percentLabel = computed(() => normalizedPercent.value === null ? '--' : String(Math.round(normalizedPercent.value)))
const voltageLabel = computed(() => {
  if (typeof props.batteryVoltage !== 'number' || !Number.isFinite(props.batteryVoltage)) return '--.-'
  return Math.max(0, props.batteryVoltage).toFixed(1)
})
const fillScale = computed(() => (normalizedPercent.value ?? 0) / 100)
const isLow = computed(() => normalizedPercent.value !== null && normalizedPercent.value <= 20)
const isCritical = computed(() => normalizedPercent.value !== null && normalizedPercent.value <= 10)
const batteryTitle = computed(() => {
  if (props.batteryError) return props.batteryError
  if (normalizedPercent.value !== null) return `电量 ${percentLabel.value}% · MapHub ${props.batteryState}`
  if (props.batteryState === 'connecting') return '正在连接 MapHub 并等待电量数据'
  if (props.batteryState === 'ok') return 'MapHub 已连接，等待电量数据'
  return '电量数据未连接'
})
</script>

<template>
  <ConsoleHudPanel variant="bare" label="VITAL">
    <div class="vitals">
      <div class="v-identity">
        <b class="v-op" title="当前操作者">{{ props.username.toUpperCase() }}</b>
        <span v-for="l in props.links" :key="l.key" class="v-link" :title="l.title">
          <i class="conn-dot" :class="`is-${l.state}`"></i>{{ l.key }}
        </span>
      </div>

      <!-- 词汇表 D+：FPS 连续条（两端 45° 切角 + 10% 刻度） -->
      <div
        class="v-bar"
        :class="{ 'is-low': isLow, 'is-critical': isCritical, 'is-pending': normalizedPercent === null }"
        role="meter"
        aria-label="机器人电池电量"
        aria-valuemin="0"
        aria-valuemax="100"
        :aria-valuenow="normalizedPercent ?? undefined"
        :aria-valuetext="normalizedPercent === null ? batteryTitle : `${percentLabel}%`"
        :title="batteryTitle"
      >
        <span class="v-track" aria-hidden="true">
          <i class="vitals-fill" :style="{ transform: `scaleX(${fillScale})` }"></i>
          <i v-for="t in ticks" :key="t" class="v-tick" :style="{ left: `${t}%` }"></i>
          <i v-if="normalizedPercent !== null" :key="normalizedPercent" class="v-sheen"></i>
          <i class="v-alert-scan"></i>
        </span>
        <b class="v-pct hud-mono">
          <Transition name="battery-value" mode="out-in">
            <span :key="percentLabel">{{ percentLabel }}</span>
          </Transition>
          <span>%</span>
        </b>
      </div>

      <div class="v-data hud-mono">
        <span class="v-voltage" title="电池电压">
          <Transition name="battery-value" mode="out-in">
            <span :key="voltageLabel">{{ voltageLabel }}</span>
          </Transition>
          <em class="hud-unit">V</em>
        </span>
        <span class="v-command" title="前端请求线速度 / 前端请求线速度上限">
          <em class="v-command-label">CMD</em>
          {{ props.speedCurrent.toFixed(2) }}/{{ props.speedLimit.toFixed(2) }}<em class="hud-unit">m/s</em>
        </span>
      </div>
    </div>
  </ConsoleHudPanel>
</template>

<style>
.vitals {
  display: flex;
  flex-direction: column;
  gap: 8px;
}

.v-identity {
  display: flex;
  align-items: center;
  gap: 10px;
}

.v-op {
  max-width: 112px;
  overflow: hidden;
  font-size: 11px;
  font-weight: 600;
  letter-spacing: 0.04em;
  color: var(--foreground);
  text-overflow: ellipsis;
  white-space: nowrap;
}

.v-link {
  display: inline-flex;
  align-items: center;
  gap: 4px;
  font-family: ui-monospace, "Cascadia Mono", Consolas, monospace;
  font-size: 9.5px;
  letter-spacing: 0.08em;
  color: var(--muted-foreground);
}

.conn-dot {
  width: 6px;
  height: 6px;
  border-radius: 50%;
  background: var(--muted-foreground);
  opacity: 0.45;
}

.conn-dot.is-ok {
  background: #16a34a;
  opacity: 1;
}

.conn-dot.is-connecting {
  background: #d97706;
  opacity: 1;
  animation: hud-breathe 1.6s ease-in-out infinite;
}

.v-bar {
  display: flex;
  align-items: center;
  gap: 10px;
}

.v-track {
  position: relative;
  width: var(--hud-bar-w);
  height: var(--hud-bar-h);
  background: var(--border);
  clip-path: polygon(6px 0, 100% 0, calc(100% - 6px) 100%, 0 100%);
  overflow: hidden;
  transition:
    background-color 0.2s ease,
    opacity 0.2s ease;
}

.vitals-fill {
  position: absolute;
  inset: 0;
  transform-origin: left center;
  background: color-mix(in srgb, var(--foreground) 75%, transparent);
  transition:
    transform 0.36s cubic-bezier(0.22, 1, 0.36, 1),
    background-color 0.2s ease,
    filter 0.2s ease;
  will-change: transform;
}

.v-tick {
  position: absolute;
  top: 0;
  bottom: 0;
  width: 1px;
  background: rgba(255, 255, 255, 0.6);
}

.v-pct {
  display: flex;
  justify-content: flex-end;
  min-width: 36px;
  overflow: hidden;
  font-size: 13px;
  font-weight: 500;
  color: var(--foreground);
  transition: color 0.2s ease;
}

.v-bar.is-pending .v-track {
  opacity: 0.48;
}

.v-bar.is-pending .v-pct {
  color: var(--muted-foreground);
}

.v-bar.is-low .vitals-fill {
  background: #dc2626;
}

.v-bar.is-low .v-pct {
  color: #dc2626;
}

.v-bar.is-critical .vitals-fill {
  filter: saturate(1.2) brightness(0.9);
}

.v-sheen,
.v-alert-scan {
  position: absolute;
  top: 0;
  bottom: 0;
  width: 28%;
  pointer-events: none;
}

.v-sheen {
  left: -35%;
  background: linear-gradient(90deg, transparent, rgba(255, 255, 255, 0.72), transparent);
  animation: battery-sheen 0.56s ease-out 1;
}

.v-alert-scan {
  left: -30%;
  background: linear-gradient(90deg, transparent, rgba(255, 255, 255, 0.92), transparent);
  opacity: 0;
}

.v-bar.is-low .v-alert-scan {
  animation: battery-alert-scan 0.66s ease-out 1;
}

.v-voltage {
  display: inline-flex;
  min-width: 48px;
}

.battery-value-enter-active,
.battery-value-leave-active {
  transition:
    opacity 0.14s ease,
    transform 0.14s ease;
}

.battery-value-enter-from {
  opacity: 0;
  transform: translateY(3px);
}

.battery-value-leave-to {
  opacity: 0;
  transform: translateY(-3px);
}

.v-data {
  display: flex;
  align-items: center;
  gap: 16px;
  font-size: 11px;
  color: var(--foreground);
}

.v-command {
  white-space: nowrap;
}

.v-command-label {
  margin-right: 4px;
  color: var(--ornament);
  font-size: 8px;
  font-style: normal;
}

@keyframes battery-sheen {
  from { transform: translateX(0); opacity: 0; }
  20% { opacity: 0.68; }
  to { transform: translateX(480%); opacity: 0; }
}

@keyframes battery-alert-scan {
  from { transform: translateX(0); opacity: 0; }
  18% { opacity: 0.95; }
  to { transform: translateX(480%); opacity: 0; }
}

@media (prefers-reduced-motion: reduce) {
  .vitals-fill,
  .v-track,
  .v-pct,
  .battery-value-enter-active,
  .battery-value-leave-active {
    transition: none;
  }

  .v-sheen,
  .v-alert-scan {
    animation: none !important;
  }
}
</style>
