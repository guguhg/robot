<script setup lang="ts">
import {
  createOverviewRuntimeScope,
  OVERVIEW_RUNTIME_MAX_AGE_MS,
  type OverviewLinkKey,
  useOverviewRuntime,
} from '@/composables/useOverviewRuntime'
import type { LinkState } from '@/composables/useRobot'
import OverviewVideoPreview from '@/components/OverviewVideoPreview.vue'
import UiTooltip from '@/components/UiTooltip.vue'

type ConsoleStage = 'video' | 'map' | 'cloud'
type OverviewTone = 'muted' | 'ready' | 'observe' | 'cached' | 'loading' | LinkState

const emit = defineEmits<{
  (e: 'enter-console', stage: ConsoleStage): void
}>()

const { session } = useAuth()
const runtimeConfig = useRuntimeConfig()
const { snapshot: runtimeSnapshot, hydrate: hydrateRuntime } = useOverviewRuntime()
const overviewVideoState = ref<LinkState>('off')
const hydrated = ref(false)
const runtimeClock = ref(Date.now())

let runtimeClockTimer: ReturnType<typeof setInterval> | undefined

const runtimeScope = computed(() => createOverviewRuntimeScope(session.value, runtimeConfig.public.apiBase))
const sessionRole = computed(() => {
  if (!session.value) {
    return { label: '未认证', detail: '登录后可进入机器人控制台。', tone: 'muted', icon: 'lucide:lock-keyhole' }
  }
  if (session.value.isGuest) {
    return { label: '观察', detail: '游客可查看控制台，但不会获得机器人操作权。', tone: 'observe', icon: 'lucide:eye' }
  }
  if (session.value.permissions.includes('robot.control')) {
    return { label: '操作员', detail: '账户具备控制权限；当前操作权仍以控制台内状态为准。', tone: 'ready', icon: 'lucide:shield-check' }
  }
  return { label: '成员', detail: '当前账户可进入控制台观察，未检测到机器人控制权限。', tone: 'observe', icon: 'lucide:user-round' }
})

const readiness = computed(() => {
  if (!session.value) return { label: '等待认证', tone: 'muted' }
  if (session.value.isGuest) return { label: '观察模式', tone: 'observe' }
  return { label: 'Stand by', tone: 'ready' }
})

const overviewVideoStatus = computed(() => {
  if (!session.value) {
    return { label: 'LOCKED', tone: 'muted', tooltip: '登录后建立概览现场视频；地图与导航仍在控制台中使用。' }
  }
  if (overviewVideoState.value === 'ok') {
    return { label: 'LIVE', tone: 'ready', tooltip: '当前概览正在显示 SRS WebRTC 视频帧。' }
  }
  if (overviewVideoState.value === 'connecting') {
    return { label: 'LINKING', tone: 'loading', tooltip: '正在连接现场视频，控制台可继续选择其他视频来源。' }
  }
  return { label: 'OFF', tone: 'muted', tooltip: '概览视频暂未建立；进入控制台可查看详细错误和切换视频来源。' }
})

const activeRuntime = computed(() => {
  const value = runtimeSnapshot.value
  if (
    !runtimeScope.value
    || value.scope !== runtimeScope.value
    || !value.updatedAt
    || runtimeClock.value - value.updatedAt > OVERVIEW_RUNTIME_MAX_AGE_MS
  ) return null
  return value
})

const runtimeStatus = computed(() => {
  if (!session.value) return { label: 'LOCKED', tone: 'muted', tooltip: '登录后才读取当前身份的最近控制台状态。' }
  if (activeRuntime.value) {
    return {
      label: 'LAST SESSION',
      tone: 'cached',
      tooltip: '视频预览保持当前实时连接；电量与其余链路来自最近一次控制台会话。',
    }
  }
  return {
    label: 'WAITING',
    tone: 'muted',
    tooltip: '尚无本次会话的遥测快照。进入控制台后会记录真实电量与链路状态。',
  }
})

const batteryPercent = computed(() => {
  const value = activeRuntime.value?.batteryPercent
  return Number.isFinite(value) ? Math.max(0, Math.min(100, value!)) : null
})
const batteryPercentLabel = computed(() => batteryPercent.value === null ? '--' : Math.round(batteryPercent.value).toString())
const batteryVoltageLabel = computed(() => {
  const value = activeRuntime.value?.batteryVoltage
  return Number.isFinite(value) ? `${value!.toFixed(2)} V` : '-- V'
})
const batteryFill = computed(() => batteryPercent.value === null ? 0 : batteryPercent.value / 100)

const connectionDefinitions: Array<{ key: OverviewLinkKey; name: string; detail: string }> = [
  { key: 'API', name: 'API', detail: '认证与服务链路' },
  { key: 'CMD', name: 'CMD', detail: 'CommandHub 控制链路' },
  { key: 'MAP', name: 'MAP', detail: 'MapHub 遥测链路' },
  { key: 'VID', name: 'VID', detail: '视频来源链路' },
  { key: 'PCD', name: 'PCD', detail: 'PointCloudHub 感知链路' },
]

const connections = computed(() => connectionDefinitions.map((definition) => {
  const runtime = activeRuntime.value
  if (runtime) {
    const state = runtime.links[definition.key]
    return {
      ...definition,
      label: linkLabel(state),
      tone: state as OverviewTone,
      tooltip: `最近控制台会话：${definition.detail}为 ${linkDescription(state)}。`,
    }
  }

  if (definition.key === 'API' && session.value) {
    return { ...definition, label: 'AUTH', tone: 'ready' as OverviewTone, tooltip: '当前页面已完成认证；服务链路将在进入控制台后确认。' }
  }
  return { ...definition, label: 'WAIT', tone: 'muted' as OverviewTone, tooltip: `${definition.detail}将在进入控制台后按需建立。` }
}))

const runtimeAge = computed(() => activeRuntime.value ? formatAge(activeRuntime.value.updatedAt, runtimeClock.value) : '--')

function linkLabel(state: LinkState) {
  if (state === 'ok') return 'ONLINE'
  if (state === 'connecting') return 'LINKING'
  return 'OFFLINE'
}

function linkDescription(state: LinkState) {
  if (state === 'ok') return '已连接'
  if (state === 'connecting') return '连接中'
  return '未连接'
}

function enterConsole(stage: ConsoleStage) {
  emit('enter-console', stage)
}

function formatAge(savedAt: number, now = Date.now()) {
  const elapsed = Math.max(0, now - savedAt)
  const minutes = Math.floor(elapsed / 60_000)
  if (minutes < 1) return '刚刚'
  if (minutes < 60) return `${minutes} 分钟前`
  const hours = Math.floor(minutes / 60)
  if (hours < 24) return `${hours} 小时前`
  return `${Math.floor(hours / 24)} 天前`
}

onMounted(() => {
  hydrated.value = true
  hydrateRuntime(runtimeScope.value)
  runtimeClockTimer = setInterval(() => (runtimeClock.value = Date.now()), 30_000)
})

watch(() => session.value?.token, () => {
  if (!hydrated.value) return
  hydrateRuntime(runtimeScope.value)
})

onUnmounted(() => {
  clearInterval(runtimeClockTimer)
})
</script>

<template>
  <section class="view-overview flex-1" aria-labelledby="overview-title">
    <div class="overview-shell">
      <header class="overview-hero">
        <div class="overview-hero-copy">
          <div class="overview-eyebrow-row">
            <span class="overview-eyebrow">MIO / FIELD OPERATIONS</span>
            <span class="overview-hero-rule" aria-hidden="true"></span>
            <span class="overview-status-pill" :class="`is-${readiness.tone}`">
              <i aria-hidden="true"></i>{{ readiness.label }}
            </span>
          </div>
          <h1 id="overview-title">Control</h1>
        </div>

        <UiTooltip side="bottom">
          <template #trigger>
            <div class="overview-identity" tabindex="0">
              <Icon :name="sessionRole.icon" size="15" aria-hidden="true" />
              <span>
                <b>{{ session?.username || '未认证身份' }}</b>
                <small>{{ sessionRole.label }}</small>
              </span>
            </div>
          </template>
          {{ sessionRole.detail }}
        </UiTooltip>
      </header>

      <div class="overview-layout">
        <section class="overview-scene" aria-labelledby="overview-scene-title">
          <div class="overview-section-head">
            <div>
              <span class="overview-eyebrow">LIVE OBSERVATION</span>
              <h2 id="overview-scene-title">设备画面</h2>
            </div>
            <UiTooltip side="left">
              <template #trigger>
                <span class="overview-scene-status" :class="`is-${overviewVideoStatus.tone}`" tabindex="0">
                  <i aria-hidden="true"></i>{{ overviewVideoStatus.label }}
                </span>
              </template>
              {{ overviewVideoStatus.tooltip }}
            </UiTooltip>
          </div>

          <div class="overview-video-host">
            <OverviewVideoPreview
              @open-console="enterConsole('video')"
              @state="overviewVideoState = $event"
            />
          </div>
        </section>

        <aside class="overview-runtime" aria-labelledby="overview-runtime-title">
          <div class="overview-section-head">
            <div>
              <span class="overview-eyebrow">RUNTIME SNAPSHOT</span>
              <h2 id="overview-runtime-title">核心状态</h2>
            </div>
            <UiTooltip side="left">
              <template #trigger>
                <span class="overview-runtime-status" :class="`is-${runtimeStatus.tone}`" tabindex="0">
                  <i aria-hidden="true"></i>{{ runtimeStatus.label }}
                </span>
              </template>
              {{ runtimeStatus.tooltip }}
            </UiTooltip>
          </div>

          <div class="overview-battery" :class="{ 'is-pending': batteryPercent === null, 'is-low': batteryPercent !== null && batteryPercent <= 20 }">
            <Icon name="lucide:battery-medium" size="19" aria-hidden="true" />
            <div class="overview-battery-copy">
              <span>电量</span>
              <div class="overview-battery-track" role="meter" aria-label="最近电池电量" aria-valuemin="0" aria-valuemax="100" :aria-valuenow="batteryPercent ?? undefined">
                <i :style="{ transform: `scaleX(${batteryFill})` }" aria-hidden="true"></i>
              </div>
            </div>
            <b>{{ batteryPercentLabel }}<small>%</small></b>
            <em>{{ batteryVoltageLabel }}</em>
          </div>

          <div class="overview-link-grid" aria-label="最近控制台链路状态">
            <UiTooltip v-for="link in connections" :key="link.key" side="top">
              <template #trigger>
                <div class="overview-link" tabindex="0">
                  <span><i class="overview-link-dot" :class="`is-${link.tone}`" aria-hidden="true"></i>{{ link.name }}</span>
                  <b :class="`is-${link.tone}`">{{ link.label }}</b>
                </div>
              </template>
              {{ link.tooltip }}
            </UiTooltip>
          </div>

          <div class="overview-runtime-foot">
            <span>更新</span><b>{{ runtimeAge }}</b>
            <span>{{ activeRuntime ? '返回控制台可刷新' : '进入控制台后读取' }}</span>
          </div>
        </aside>
      </div>
    </div>
  </section>
</template>

<style>
.view-overview { display: flex; min-height: 0; background: linear-gradient(180deg, #ffffff 0, #ffffff 72%, #f8fafc 100%); }
.overview-shell { box-sizing: border-box; width: min(1440px, 100%); min-height: calc(100dvh - var(--nav-h)); margin: 0 auto; padding: 24px 24px 20px; }
.overview-hero, .overview-eyebrow-row, .overview-identity, .overview-section-head, .overview-battery, .overview-link, .overview-runtime-foot { display: flex; align-items: center; }
.overview-hero { justify-content: space-between; min-height: 82px; gap: 24px; padding-bottom: 18px; border-bottom: 1px solid var(--border); }
.overview-hero-copy { min-width: 0; }
.overview-eyebrow-row { gap: 9px; margin-bottom: 8px; }
.overview-eyebrow { color: var(--muted-foreground); font-family: ui-monospace, "Cascadia Mono", Consolas, monospace; font-size: 9px; font-weight: 600; letter-spacing: 0; line-height: 1; }
.overview-hero-rule { width: 26px; height: 1px; background: var(--border); }
.overview-status-pill, .overview-scene-status, .overview-runtime-status { display: inline-flex; align-items: center; gap: 5px; color: var(--muted-foreground); font-family: ui-monospace, "Cascadia Mono", Consolas, monospace; font-size: 9px; letter-spacing: 0; line-height: 1; white-space: nowrap; }
.overview-status-pill i, .overview-scene-status i, .overview-runtime-status i, .overview-link-dot { width: 6px; height: 6px; flex: none; border-radius: 50%; background: #98a2b3; }
.overview-status-pill.is-ready i, .overview-runtime-status.is-ready i, .overview-link-dot.is-ready { background: #16a34a; }
.overview-status-pill.is-observe i { background: #2563eb; }
.overview-scene-status.is-ready i { background: #16a34a; }
.overview-scene-status.is-loading i { background: #d97706; animation: overview-breathe 1.6s ease-in-out infinite; }
.overview-link-dot.is-ok { background: #16a34a; }
.overview-link-dot.is-connecting { background: #d97706; animation: overview-breathe 1.6s ease-in-out infinite; }
.overview-hero h1, .overview-section-head h2 { margin: 0; color: var(--foreground); letter-spacing: 0; }
.overview-hero h1 { font-size: 36px; font-weight: 650; line-height: 1.06; }
.overview-identity { min-width: 142px; max-width: 220px; gap: 8px; color: var(--muted-foreground); cursor: help; outline: none; }
.overview-identity > span { display: grid; min-width: 0; gap: 3px; }
.overview-identity b { overflow: hidden; color: var(--foreground); font-size: 12px; font-weight: 650; text-overflow: ellipsis; white-space: nowrap; }
.overview-identity small { color: var(--muted-foreground); font-size: 10px; }
.overview-layout { display: grid; grid-template-columns: minmax(0, 1.55fr) minmax(292px, 0.7fr); align-items: stretch; gap: 28px; padding-top: 20px; }
.overview-scene, .overview-runtime { min-width: 0; }
.overview-section-head { justify-content: space-between; min-height: 34px; gap: 12px; margin-bottom: 10px; }
.overview-section-head .overview-eyebrow { display: block; margin-bottom: 5px; }
.overview-section-head h2 { font-size: 15px; font-weight: 650; line-height: 1.2; }
.overview-scene-status, .overview-runtime-status { min-height: 24px; padding: 0 7px; border: 1px solid var(--border); border-radius: 999px; background: #fff; cursor: help; outline: none; transition: border-color 0.15s ease, background-color 0.15s ease; }
.overview-scene-status:hover, .overview-scene-status:focus-visible, .overview-runtime-status:hover, .overview-runtime-status:focus-visible { border-color: color-mix(in srgb, var(--primary) 38%, var(--border)); background: color-mix(in srgb, var(--primary) 5%, #ffffff); }
.overview-video-host { display: flex; min-height: 0; flex: 1 1 auto; }
.overview-video-host > .overview-video-preview { width: 100%; height: 100%; min-height: 0; aspect-ratio: auto; }
.overview-runtime { display: flex; flex-direction: column; padding-left: 28px; border-left: 1px solid var(--border); }
.overview-battery { min-height: 66px; gap: 9px; padding: 10px 0; border-top: 1px solid var(--border); border-bottom: 1px solid var(--border); color: var(--primary); }
.overview-battery-copy { display: grid; flex: 1; min-width: 0; gap: 7px; }
.overview-battery-copy > span { color: var(--muted-foreground); font-size: 9px; }
.overview-battery-track { position: relative; width: 100%; height: 6px; overflow: hidden; background: var(--border); clip-path: polygon(5px 0, 100% 0, calc(100% - 5px) 100%, 0 100%); }
.overview-battery-track i { position: absolute; inset: 0; transform-origin: left center; background: color-mix(in srgb, var(--primary) 82%, #ffffff); transition: transform 0.36s cubic-bezier(0.22, 1, 0.36, 1), background-color 0.2s ease; }
.overview-battery > b { color: var(--foreground); font-family: ui-monospace, "Cascadia Mono", Consolas, monospace; font-size: 18px; font-weight: 650; }
.overview-battery > b small { color: var(--muted-foreground); font-size: 10px; }
.overview-battery > em { min-width: 37px; color: var(--muted-foreground); font-family: ui-monospace, "Cascadia Mono", Consolas, monospace; font-size: 9px; font-style: normal; text-align: right; }
.overview-battery.is-pending { color: var(--muted-foreground); }
.overview-battery.is-pending .overview-battery-track { opacity: 0.45; }
.overview-battery.is-low { color: #dc2626; }
.overview-battery.is-low .overview-battery-track i { background: #dc2626; }
.overview-link-grid { display: grid; grid-template-columns: repeat(5, minmax(0, 1fr)); margin-top: 10px; border-top: 1px solid var(--border); border-bottom: 1px solid var(--border); }
.overview-link { display: grid; min-width: 0; min-height: 51px; align-content: center; gap: 5px; padding: 7px 5px; border-right: 1px solid var(--border); cursor: help; outline: none; transition: background-color 0.15s ease; }
.overview-link:last-child { border-right: 0; }
.overview-link:hover, .overview-link:focus-visible { background: color-mix(in srgb, var(--primary) 4%, transparent); }
.overview-link span { display: inline-flex; align-items: center; gap: 4px; color: var(--muted-foreground); font-family: ui-monospace, "Cascadia Mono", Consolas, monospace; font-size: 8px; }
.overview-link b { overflow: hidden; color: var(--muted-foreground); font-family: ui-monospace, "Cascadia Mono", Consolas, monospace; font-size: 8px; font-weight: 600; text-overflow: ellipsis; white-space: nowrap; }
.overview-link b.is-ok, .overview-link b.is-ready { color: #15803d; }
.overview-link b.is-connecting { color: #b45309; }
.overview-link b.is-cached { color: var(--primary); }
.overview-runtime-foot { justify-content: space-between; gap: 8px; min-height: 28px; color: var(--muted-foreground); font-size: 9px; }
.overview-runtime-foot b { color: var(--foreground); font-family: ui-monospace, "Cascadia Mono", Consolas, monospace; font-size: 9px; font-weight: 600; }
.overview-runtime-foot > span:last-child { margin-left: auto; text-align: right; }
@keyframes overview-breathe { 0%, 100% { opacity: 1; } 50% { opacity: 0.35; } }
@media (min-width: 861px) {
  .overview-shell { display: flex; flex-direction: column; }
  .overview-layout { flex: 1 1 auto; min-height: 0; grid-template-columns: minmax(0, 1.8fr) minmax(340px, 0.72fr); gap: 36px; padding-top: 24px; }
  .overview-scene { display: flex; flex-direction: column; min-height: 0; }
  .overview-video-host { min-height: clamp(248px, 34vh, 360px); }
  .overview-runtime { min-height: 0; padding-left: 36px; }
  .overview-runtime-foot { margin-top: auto; padding-top: 10px; border-top: 1px solid var(--border); }
}
@media (max-width: 860px) {
  .overview-layout { grid-template-columns: 1fr; gap: 20px; }
  .overview-runtime { padding: 18px 0 0; border-top: 1px solid var(--border); border-left: 0; }
}
@media (max-width: 720px) {
  .overview-shell { min-height: calc(100dvh - var(--nav-h)); padding: 16px 16px 18px; }
  .overview-hero { min-height: 64px; padding-bottom: 14px; }
  .overview-hero h1 { font-size: 28px; }
  .overview-identity { min-width: 88px; max-width: 112px; }
  .overview-identity b { font-size: 10px; }
  .overview-identity small { font-size: 9px; }
  .overview-hero-rule { width: 14px; }
  .overview-layout { gap: 16px; padding-top: 16px; }
  .overview-video-host { min-height: 0; }
  .overview-video-host > .overview-video-preview { height: auto; aspect-ratio: 16 / 10; }
  .overview-runtime { padding-top: 16px; }
  .overview-link { min-height: 47px; padding: 6px 3px; }
  .overview-link span, .overview-link b { font-size: 7px; }
}
@media (hover: none), (pointer: coarse) {
  .overview-link, .overview-identity { cursor: default; }
}
@media (prefers-reduced-motion: reduce) {
  .overview-scene-status.is-loading i, .overview-link-dot.is-connecting { animation: none; }
  .overview-scene-status, .overview-runtime-status, .overview-link, .overview-battery-track i { transition: none; }
}
</style>
