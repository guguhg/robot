<script setup lang="ts">
import {
  TELEMETRY_STALE_AFTER_MS,
  type FrameMetric,
  type MapFrameSource,
  type TelemetryMetricKey,
} from '@/composables/useTelemetry'
import type { LinkState } from '@/composables/useRobot'

interface PointCloudDiagnostics {
  state: LinkState
  streamName: string
  topic: string
  displayedPointCount: number
  sourcePointCount: number
  validPointCount: number
  filteredPointCount: number
  dataFps: number
  renderFps: number
  renderMode: 'active' | 'idle'
  lastFrameAt: number
  paused: boolean
  frameHeld: boolean
  cachedSnapshot: boolean
  awaitingFirstFrame: boolean
  firstFrameTimedOut: boolean
  error: string
  maxVisiblePoints: number
  colorMode: string
  viewPreset: string
}

interface VideoDiagnostics {
  state: LinkState
  source: string
  sourceMeta: string
  fps: number | null
  frameHeld: boolean
  error: string
}

interface ErrorEntry {
  at: number
  source: string
  message: string
}

const props = withDefaults(defineProps<{
  apiState: LinkState
  cmdState: LinkState
  mapState: LinkState
  videoState: LinkState
  pointCloudState: LinkState
  mapFrameSource?: MapFrameSource
  mapCachedAt?: number
  telemetryMetrics: Record<TelemetryMetricKey, FrameMetric>
  pointCloud: PointCloudDiagnostics
  video: VideoDiagnostics
  telemetryError?: string
  controlError?: string
}>(), {
  telemetryError: '',
  controlError: '',
  mapFrameSource: 'none',
  mapCachedAt: 0,
})

const metricDefs: Array<{
  key: TelemetryMetricKey
  label: string
  staleMs: number
  staticFrame?: boolean
}> = [
  { key: 'map', label: '地图', staleMs: TELEMETRY_STALE_AFTER_MS.map, staticFrame: true },
  { key: 'robotPose', label: '机器人位姿', staleMs: TELEMETRY_STALE_AFTER_MS.robotPose },
  { key: 'scan', label: '激光扫描', staleMs: TELEMETRY_STALE_AFTER_MS.scan },
  { key: 'path', label: '全局路径', staleMs: TELEMETRY_STALE_AFTER_MS.path },
  { key: 'localPlan', label: '局部路径', staleMs: TELEMETRY_STALE_AFTER_MS.localPlan },
  { key: 'particles', label: '定位粒子', staleMs: TELEMETRY_STALE_AFTER_MS.particles },
  { key: 'globalCostmap', label: '全局代价图', staleMs: TELEMETRY_STALE_AFTER_MS.globalCostmap },
  { key: 'localCostmap', label: '局部代价图', staleMs: TELEMETRY_STALE_AFTER_MS.localCostmap },
  { key: 'mapPatch', label: '地图更新', staleMs: TELEMETRY_STALE_AFTER_MS.mapPatch },
]

const now = ref(Date.now())
const errorHistory = ref<ErrorEntry[]>([])
const activeErrorBySource = new Map<string, string>()
let clockTimer: ReturnType<typeof setInterval> | undefined

const connections = computed(() => [
  { key: 'API', label: 'C.WebApi', state: props.apiState },
  { key: 'CMD', label: 'CommandHub', state: props.cmdState },
  { key: 'MAP', label: 'MapHub', state: props.mapState },
  { key: 'VID', label: '视频', state: props.videoState },
  { key: 'PCD', label: '点云', state: props.pointCloudState },
])

const incomingErrors = computed(() => [
  { source: 'CommandHub', message: props.controlError },
  { source: 'MapHub', message: props.telemetryError },
  { source: 'Video', message: props.video.error },
  { source: 'PointCloudHub', message: props.pointCloud.error },
])

watch(incomingErrors, (entries) => {
  for (const entry of entries) {
    const message = entry.message.trim()
    if (!message) {
      activeErrorBySource.delete(entry.source)
      continue
    }
    if (activeErrorBySource.get(entry.source) === message) continue
    activeErrorBySource.set(entry.source, message)
    errorHistory.value.unshift({ at: Date.now(), source: entry.source, message })
  }
  if (errorHistory.value.length > 6) errorHistory.value.length = 6
}, { immediate: true })

function stateLabel(state: LinkState) {
  if (state === 'ok') return 'ONLINE'
  if (state === 'connecting') return 'LINKING'
  return 'OFFLINE'
}

function metricState(definition: typeof metricDefs[number]) {
  const metric = props.telemetryMetrics[definition.key]
  if (!metric.lastAt) return { label: 'WAIT', className: 'is-wait' }
  if (definition.staticFrame) {
    return props.mapFrameSource === 'cached'
      ? { label: 'CACHED', className: 'is-cached' }
      : { label: 'STATIC', className: 'is-fresh' }
  }
  if (now.value - metric.lastAt <= definition.staleMs) return { label: 'FRESH', className: 'is-fresh' }
  return { label: 'STALE', className: 'is-stale' }
}

function formatAge(lastAt: number) {
  if (!lastAt) return '--'
  const seconds = Math.max(0, (now.value - lastAt) / 1000)
  if (seconds < 1) return '<1s'
  if (seconds < 60) return `${seconds.toFixed(seconds < 10 ? 1 : 0)}s`
  return `${Math.floor(seconds / 60)}m`
}

function formatHz(metric: FrameMetric) {
  if (metric.count < 2 || metric.hz <= 0) return '-- Hz'
  return `${metric.hz.toFixed(metric.hz < 10 ? 1 : 0)} Hz`
}

function formatFps(value: number) {
  return Number.isFinite(value) && value > 0 ? value.toFixed(value < 10 ? 1 : 0) : '--'
}

function formatRatio(value: number, total: number) {
  if (!Number.isFinite(value) || !Number.isFinite(total) || total <= 0) return '--'
  return `${Math.max(0, Math.min(100, (value / total) * 100)).toFixed(1)}%`
}

function pointCloudPresetLabel(preset: string) {
  if (preset === 'scan-top') return 'TOP'
  if (preset === 'points-3d') return '3D'
  return 'CUSTOM'
}

function pointCloudFrameState() {
  if (props.pointCloud.cachedSnapshot) return `CACHE ${formatAge(props.pointCloud.lastFrameAt)}`
  if (props.pointCloud.frameHeld) return `HOLD ${formatAge(props.pointCloud.lastFrameAt)}`
  if (props.pointCloud.awaitingFirstFrame) return props.pointCloud.firstFrameTimedOut ? 'NO FRAME' : 'WAIT FRAME'
  if (props.pointCloud.paused) return 'FROZEN'
  return `CUT ${Math.max(0, props.pointCloud.validPointCount - props.pointCloud.filteredPointCount)}`
}

function downloadDiagnostics() {
  const snapshot = {
    schemaVersion: 1,
    generatedAt: new Date().toISOString(),
    connections: Object.fromEntries(connections.value.map(item => [item.key, item.state])),
    video: { ...props.video },
    pointCloud: { ...props.pointCloud },
    mapSnapshot: {
      source: props.mapFrameSource,
      cachedAt: props.mapCachedAt ? new Date(props.mapCachedAt).toISOString() : null,
    },
    telemetry: Object.fromEntries(metricDefs.map(({ key }) => [key, { ...props.telemetryMetrics[key] }])),
    errors: errorHistory.value.map(entry => ({ ...entry, at: new Date(entry.at).toISOString() })),
  }
  const url = URL.createObjectURL(new Blob([JSON.stringify(snapshot, null, 2)], { type: 'application/json' }))
  const anchor = document.createElement('a')
  anchor.href = url
  anchor.download = `zerorobot-diagnostics-${new Date().toISOString().replace(/[:.]/g, '-')}.json`
  anchor.click()
  setTimeout(() => URL.revokeObjectURL(url), 0)
}

onMounted(() => {
  clockTimer = setInterval(() => (now.value = Date.now()), 1000)
})

onUnmounted(() => clearInterval(clockTimer))
</script>

<template>
  <section class="diagnostics-panel" aria-label="前端诊断中心">
    <header class="diagnostics-head">
      <div class="media-toolbar-heading">
        <span class="media-status-dot" :class="`is-${mapState}`" aria-hidden="true"></span>
        <span class="media-toolbar-title hud-mono">DIAGNOSTICS</span>
        <span class="diagnostics-scope hud-mono">LOCAL</span>
      </div>
      <button type="button" title="导出诊断 JSON" aria-label="导出诊断 JSON" @click="downloadDiagnostics">
        <Icon name="lucide:download" size="15" />
      </button>
    </header>

    <section class="diagnostics-section" aria-labelledby="diagnostics-links-title">
      <h3 id="diagnostics-links-title" class="diagnostics-label hud-mono">LINKS</h3>
      <div class="diagnostics-links">
        <div v-for="link in connections" :key="link.key" class="diagnostics-link">
          <span class="media-status-dot" :class="`is-${link.state}`" aria-hidden="true"></span>
          <span>{{ link.label }}</span>
          <strong class="hud-mono">{{ stateLabel(link.state) }}</strong>
        </div>
      </div>
    </section>

    <section class="diagnostics-section" aria-labelledby="diagnostics-frames-title">
      <h3 id="diagnostics-frames-title" class="diagnostics-label hud-mono">MAP FRAMES</h3>
      <div class="diagnostics-metrics">
        <div v-for="definition in metricDefs" :key="definition.key" class="diagnostics-metric">
          <span class="diagnostics-metric-name">{{ definition.label }}</span>
          <span class="diagnostics-metric-hz hud-mono">{{ formatHz(telemetryMetrics[definition.key]) }}</span>
          <span class="diagnostics-metric-age hud-mono">{{ formatAge(telemetryMetrics[definition.key].lastAt) }}</span>
          <strong class="diagnostics-metric-state hud-mono" :class="metricState(definition).className">
            {{ metricState(definition).label }}
          </strong>
        </div>
      </div>
    </section>

    <section class="diagnostics-section" aria-labelledby="diagnostics-media-title">
      <h3 id="diagnostics-media-title" class="diagnostics-label hud-mono">MEDIA</h3>
      <div class="diagnostics-media-row">
        <span>视频 · {{ video.source || '--' }}</span>
        <strong class="hud-mono">{{ video.frameHeld ? 'FRAME HOLD' : `${video.fps === null ? '--' : video.fps.toFixed(1)} FPS` }}</strong>
      </div>
      <div class="diagnostics-media-row">
        <span>点云 · {{ pointCloud.streamName || '--' }} · {{ pointCloudPresetLabel(pointCloud.viewPreset) }}</span>
        <strong class="hud-mono">{{ pointCloud.displayedPointCount.toLocaleString() }} PTS</strong>
      </div>
      <div class="diagnostics-performance hud-mono">
        <span>DATA {{ formatFps(pointCloud.dataFps) }}</span>
        <span>{{ pointCloud.renderMode === 'idle' ? 'DRAW IDLE' : `DRAW ${formatFps(pointCloud.renderFps)}` }}</span>
        <span>VALID {{ formatRatio(pointCloud.validPointCount, pointCloud.sourcePointCount) }}</span>
        <span>{{ pointCloudFrameState() }}</span>
      </div>
    </section>

    <section class="diagnostics-section" aria-labelledby="diagnostics-errors-title">
      <h3 id="diagnostics-errors-title" class="diagnostics-label hud-mono">RECENT ERRORS</h3>
      <div v-if="errorHistory.length" class="diagnostics-errors">
        <div v-for="entry in errorHistory" :key="`${entry.at}-${entry.source}`" class="diagnostics-error">
          <span class="hud-mono">{{ entry.source }} · {{ new Date(entry.at).toLocaleTimeString('zh-CN', { hour12: false }) }}</span>
          <strong>{{ entry.message }}</strong>
        </div>
      </div>
      <div v-else class="diagnostics-clear">
        <Icon name="lucide:check" size="13" />
        <span>暂无错误</span>
      </div>
    </section>
  </section>
</template>

<style scoped>
.diagnostics-panel {
  display: grid;
  gap: 12px;
  min-width: 0;
  color: var(--foreground);
}

.diagnostics-head,
.diagnostics-link,
.diagnostics-metric,
.diagnostics-media-row,
.diagnostics-performance,
.diagnostics-clear {
  display: flex;
  align-items: center;
}

.diagnostics-head {
  justify-content: space-between;
  min-height: 28px;
}

.diagnostics-head button {
  display: inline-flex;
  align-items: center;
  justify-content: center;
  width: 28px;
  height: 28px;
  border: 0;
  border-radius: 4px;
  background: transparent;
  color: var(--muted-foreground);
  cursor: pointer;
}

.diagnostics-head button:hover,
.diagnostics-head button:focus-visible {
  background: color-mix(in srgb, var(--foreground) 8%, transparent);
  color: var(--foreground);
}

.diagnostics-scope {
  color: var(--muted-foreground);
  font-size: 8px;
}

.diagnostics-section {
  display: grid;
  gap: 7px;
  padding-top: 10px;
  border-top: 1px solid color-mix(in srgb, var(--border) 72%, transparent);
}

.diagnostics-label {
  margin: 0;
  color: var(--ornament);
  font-size: 8px;
  font-weight: 500;
}

.diagnostics-links,
.diagnostics-metrics,
.diagnostics-errors {
  display: grid;
  gap: 5px;
}

.diagnostics-link {
  min-height: 20px;
  gap: 7px;
  color: var(--muted-foreground);
  font-size: 9px;
}

.diagnostics-link strong {
  margin-left: auto;
  color: var(--foreground);
  font-size: 8px;
  font-weight: 500;
}

.diagnostics-metric {
  display: grid;
  grid-template-columns: minmax(72px, 1fr) 48px 34px 44px;
  gap: 5px;
  min-height: 20px;
  color: var(--muted-foreground);
  font-size: 8.5px;
}

.diagnostics-metric-name {
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}

.diagnostics-metric-hz,
.diagnostics-metric-age,
.diagnostics-metric-state {
  text-align: right;
  white-space: nowrap;
}

.diagnostics-metric-state {
  color: #98a2b3;
  font-size: 7.5px;
  font-weight: 500;
}

.diagnostics-metric-state.is-fresh { color: #16a34a; }
.diagnostics-metric-state.is-cached { color: #2563eb; }
.diagnostics-metric-state.is-stale { color: #d97706; }

.diagnostics-media-row {
  justify-content: space-between;
  gap: 8px;
  min-width: 0;
  color: var(--muted-foreground);
  font-size: 9px;
}

.diagnostics-media-row > span {
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}

.diagnostics-media-row strong {
  flex: none;
  color: var(--foreground);
  font-size: 8.5px;
  font-weight: 500;
}

.diagnostics-performance {
  justify-content: space-between;
  flex-wrap: wrap;
  gap: 6px;
  padding-top: 2px;
  color: var(--ornament);
  font-size: 7.5px;
}

.diagnostics-error {
  display: grid;
  gap: 3px;
}

.diagnostics-error span {
  color: #dc2626;
  font-size: 7.5px;
}

.diagnostics-error strong {
  color: var(--muted-foreground);
  font-size: 8.5px;
  font-weight: 400;
  line-height: 1.4;
  overflow-wrap: anywhere;
}

.diagnostics-clear {
  gap: 6px;
  min-height: 24px;
  color: #16a34a;
  font-size: 9px;
}
</style>
