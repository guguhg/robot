<script setup lang="ts">
import {
  CWebApiError,
  type DeviceInfo,
} from '@/lib/cwebapi/cwebapi-client'
import type { LinkState } from '@/composables/useRobot'
import { createWhepReceiver } from '@/lib/whep-player'
import UiTooltip from '@/components/UiTooltip.vue'

const emit = defineEmits<{
  (event: 'open-console'): void
  (event: 'state', value: LinkState): void
}>()

const { session, hasPermission, createAuthenticatedClient } = useAuth()
const runtimeConfig = useRuntimeConfig()
const apiBase = String(runtimeConfig.public.apiBase || '')
const srsBaseUrl = String((runtimeConfig.public as { srsBaseUrl?: string }).srsBaseUrl || '')

const videoEl = ref<HTMLVideoElement | null>(null)
const state = ref<LinkState>('off')
const errorMessage = ref('')
const loading = ref(false)
const hasFrame = ref(false)
const frameHeld = ref(false)
const frameRate = ref<number | null>(null)
const frameWidth = ref(0)
const frameHeight = ref(0)
const selectedDevice = ref<DeviceInfo | null>(null)
const whepOrigin = ref('')

let active = false
let generation = 0
let player: ReturnType<typeof createWhepReceiver> | null = null
let stopSessionWatch: (() => void) | undefined
let mediaTimer: ReturnType<typeof setTimeout> | undefined
let staleTimer: ReturnType<typeof setTimeout> | undefined
let frameRateTimer: ReturnType<typeof setInterval> | undefined
let videoFrameCallbackId: number | undefined
let frameWindowStarted = 0
let frameWindowCount = 0

const stateLabel = computed(() => {
  if (frameHeld.value) return 'FRAME HOLD'
  if (state.value === 'ok') return 'LIVE'
  if (state.value === 'connecting') return 'LINKING'
  return 'OFF'
})

const frameSizeLabel = computed(() => (
  frameWidth.value > 0 && frameHeight.value > 0
    ? `${frameWidth.value}×${frameHeight.value}`
    : 'NO FRAME'
))

const frameMeta = computed(() => (
  frameRate.value === null ? frameSizeLabel.value : `${frameSizeLabel.value} · ${frameRate.value} FPS`
))

const emptyTitle = computed(() => {
  if (!session.value) return '登录后查看现场视频'
  if (!hasPermission('stream.play')) return session.value.isGuest ? '游客账户不支持视频播放' : '当前身份没有视频权限'
  if (errorMessage.value) return errorMessage.value
  if (loading.value || state.value === 'connecting') return '正在连接现场视频'
  return '现场视频暂不可用'
})

const emptyIcon = computed(() => {
  if (!session.value || !hasPermission('stream.play')) return 'lucide:lock-keyhole'
  if (loading.value || state.value === 'connecting') return 'lucide:loader-circle'
  return 'lucide:video-off'
})

function clearTimer(timer: ReturnType<typeof setTimeout> | undefined) {
  if (timer !== undefined) clearTimeout(timer)
}

function stopFrameRateSampler() {
  const video = videoEl.value as (HTMLVideoElement & {
    cancelVideoFrameCallback?: (handle: number) => void
  }) | null
  if (videoFrameCallbackId !== undefined) video?.cancelVideoFrameCallback?.(videoFrameCallbackId)
  videoFrameCallbackId = undefined
  clearInterval(frameRateTimer)
  frameRateTimer = undefined
  frameRate.value = null
  frameWindowStarted = 0
  frameWindowCount = 0
}

function clearTransport() {
  clearTimer(mediaTimer)
  clearTimer(staleTimer)
  mediaTimer = undefined
  staleTimer = undefined
  stopFrameRateSampler()
  const current = player
  player = null
  current?.stop()
  if (videoEl.value) videoEl.value.srcObject = null
  hasFrame.value = false
  frameHeld.value = false
  frameWidth.value = 0
  frameHeight.value = 0
  whepOrigin.value = ''
}

function recordFrameSize() {
  const video = videoEl.value
  if (!video || video.videoWidth <= 0 || video.videoHeight <= 0) return
  frameWidth.value = video.videoWidth
  frameHeight.value = video.videoHeight
}

function markFrameArrived(version: number) {
  if (!active || version !== generation || !player) return
  hasFrame.value = true
  frameHeld.value = false
  state.value = 'ok'
  errorMessage.value = ''
  recordFrameSize()
  clearTimer(staleTimer)
  staleTimer = setTimeout(() => {
    if (!active || version !== generation || !player || !hasFrame.value) return
    frameHeld.value = true
    state.value = 'connecting'
    stopFrameRateSampler()
  }, 2500)
}

function countFrame(version: number) {
  if (!active || version !== generation || !player) return
  const now = performance.now()
  if (!frameWindowStarted) frameWindowStarted = now
  frameWindowCount += 1
  const elapsed = now - frameWindowStarted
  if (elapsed < 500) return
  frameRate.value = Math.min(240, Math.max(0, Math.round(frameWindowCount * 1000 / elapsed)))
  frameWindowStarted = now
  frameWindowCount = 0
}

function startFrameRateSampler(version: number) {
  stopFrameRateSampler()
  const video = videoEl.value as (HTMLVideoElement & {
    requestVideoFrameCallback?: (callback: () => void) => number
    webkitDecodedFrameCount?: number
  }) | null
  if (!video) return

  if (typeof video.requestVideoFrameCallback === 'function') {
    const onFrame = () => {
      if (!active || version !== generation || !player) return
      markFrameArrived(version)
      countFrame(version)
      videoFrameCallbackId = video.requestVideoFrameCallback!(onFrame)
    }
    videoFrameCallbackId = video.requestVideoFrameCallback(onFrame)
    return
  }

  const readDecodedFrames = () => {
    const quality = video.getVideoPlaybackQuality?.()
    const total = quality?.totalVideoFrames ?? video.webkitDecodedFrameCount
    return Number.isFinite(total) ? Number(total) : null
  }
  const initial = readDecodedFrames()
  if (initial === null) return
  let previous = initial
  let previousAt = performance.now()
  frameRateTimer = setInterval(() => {
    if (!active || version !== generation || !player || video.paused) return
    const total = readDecodedFrames()
    const now = performance.now()
    if (total === null || total < previous) {
      previous = total ?? previous
      previousAt = now
      return
    }
    const elapsed = now - previousAt
    if (elapsed < 400) return
    if (total > previous) {
      frameRate.value = Math.min(240, Math.max(0, Math.round((total - previous) * 1000 / elapsed)))
      markFrameArrived(version)
    }
    previous = total
    previousAt = now
  }, 500)
}

function handlePlaying() {
  if (!active || !player) return
  const version = generation
  markFrameArrived(version)
  startFrameRateSampler(version)
}

function handleWaiting() {
  if (!active || !player || hasFrame.value) return
  state.value = 'connecting'
  clearTimer(mediaTimer)
  const version = generation
  mediaTimer = setTimeout(() => {
    if (!active || version !== generation || hasFrame.value) return
    state.value = 'off'
    errorMessage.value = 'SRS WebRTC 已连通，但暂未收到视频帧。'
  }, 6000)
}

function handlePeerState(next: RTCPeerConnectionState, version: number) {
  if (!active || version !== generation || !player) return
  if (next === 'connected') {
    const video = videoEl.value
    if (video && video.readyState >= 2 && video.videoWidth > 0) handlePlaying()
    else state.value = 'connecting'
    return
  }
  if (next === 'new' || next === 'connecting') {
    state.value = 'connecting'
    return
  }
  if (next === 'failed' || next === 'disconnected' || next === 'closed') {
    state.value = 'off'
    if (!frameHeld.value) errorMessage.value = 'SRS WebRTC 连接已中断。'
  }
}

function describeError(error: unknown) {
  if (!(error instanceof CWebApiError)) return error instanceof Error ? error.message : '视频连接失败。'
  if (error.status === 400 || error.status === 404) return 'SRS 当前没有活动推流。'
  if (error.status === 403) return '当前身份没有视频播放权限。'
  if (error.status === 503) return '视频服务暂不可用。'
  if (error.status === 0) return '无法连接视频服务。'
  return error.message || '视频连接失败。'
}

async function connect() {
  const version = ++generation
  clearTransport()
  errorMessage.value = ''
  state.value = 'off'
  selectedDevice.value = null
  if (!active || !session.value) return
  if (!hasPermission('stream.play')) {
    errorMessage.value = session.value.isGuest ? '游客账户不支持视频播放。' : '当前身份没有视频播放权限。'
    emit('state', 'off')
    return
  }
  if (!videoEl.value) return

  loading.value = true
  state.value = 'connecting'
  try {
    const devices = await createAuthenticatedClient().getDevices({ signal: AbortSignal.timeout(8000) })
    if (!active || version !== generation) return
    const device = devices.find(item => item.isOnline) ?? devices[0]
    if (!device) {
      errorMessage.value = '暂无可用设备。'
      state.value = 'off'
      return
    }
    selectedDevice.value = device
    const playerRef = { value: null as ReturnType<typeof createWhepReceiver> | null }
    const nextPlayer = createWhepReceiver(createAuthenticatedClient(), videoEl.value, {
      apiBase,
      srsBaseUrl,
      onStateChange: next => {
        if (playerRef.value !== player || version !== generation) return
        handlePeerState(next, version)
      },
      onResolvedUrl: url => {
        if (playerRef.value !== player || version !== generation) return
        whepOrigin.value = new URL(url).origin
      },
    })
    playerRef.value = nextPlayer
    player = nextPlayer
    const result = await nextPlayer.start(device.deviceId)
    if (!active || version !== generation || player !== nextPlayer) {
      nextPlayer.stop()
      return
    }
    whepOrigin.value = new URL(result.resolvedWhepUrl).origin
    clearTimer(mediaTimer)
    mediaTimer = setTimeout(() => {
      if (!active || version !== generation || hasFrame.value) return
      state.value = 'off'
      errorMessage.value = 'SRS WebRTC 已连接，但暂未收到视频帧。'
    }, 6000)
  } catch (error) {
    if (!active || version !== generation) return
    state.value = 'off'
    errorMessage.value = describeError(error)
    clearTransport()
  } finally {
    if (version === generation) loading.value = false
  }
}

function startPreview() {
  if (active) return
  active = true
  void connect()
}

function stopPreview() {
  active = false
  generation += 1
  clearTransport()
  loading.value = false
  state.value = 'off'
}

function reload() {
  if (!active || loading.value) return
  void connect()
}

watch(state, value => emit('state', value), { immediate: true })

onMounted(() => {
  active = true
  stopSessionWatch = watch(
    () => session.value?.token,
    () => void connect(),
    { immediate: true },
  )
})

onActivated(() => startPreview())
onDeactivated(stopPreview)
onUnmounted(() => {
  stopSessionWatch?.()
  stopPreview()
})
</script>

<template>
  <section class="overview-video-preview" :class="{ 'has-frame': hasFrame }" aria-label="概览现场视频">
    <video
      ref="videoEl"
      class="overview-video-surface"
      autoplay
      muted
      playsinline
      @playing="handlePlaying"
      @waiting="handleWaiting"
    ></video>

    <div class="overview-video-frame" aria-hidden="true"></div>
    <button class="overview-video-hit" type="button" aria-label="进入控制台视频视图" @click="emit('open-console')"></button>

    <div class="overview-video-head" aria-hidden="true">
      <span class="overview-video-kicker">LIVE CAMERA</span>
      <span class="overview-video-status" :class="`is-${frameHeld ? 'held' : state}`">
        <i></i>{{ stateLabel }}
      </span>
    </div>

    <div v-if="!hasFrame" class="overview-video-empty" role="status">
      <Icon :class="{ 'overview-video-spin': state === 'connecting' }" :name="emptyIcon" size="22" aria-hidden="true" />
      <strong>{{ emptyTitle }}</strong>
      <small>{{ session ? (selectedDevice?.name || 'SRS WHEP') : '现场画面将在登录后建立' }}</small>
    </div>

    <div class="overview-video-foot hud-mono" aria-live="polite">
      <span>{{ frameMeta }}</span>
      <span>{{ selectedDevice?.name || 'NO DEVICE' }}</span>
      <span>{{ whepOrigin || 'SRS WHEP' }}</span>
    </div>

    <UiTooltip side="left">
      <template #trigger>
        <button class="overview-video-reload" type="button" :disabled="loading" aria-label="重新连接视频" title="重新连接视频" @click.stop="reload">
          <Icon :class="{ 'overview-video-spin': loading || state === 'connecting' }" name="lucide:refresh-cw" size="14" aria-hidden="true" />
        </button>
      </template>
      重新连接现场视频
    </UiTooltip>

    <span class="overview-video-open" aria-hidden="true"><Icon name="lucide:arrow-up-right" size="14" /></span>
  </section>
</template>

<style scoped>
.overview-video-preview {
  position: relative;
  display: block;
  width: 100%;
  min-height: 0;
  overflow: hidden;
  aspect-ratio: 16 / 9;
  border: 1px solid #d9e0e8;
  border-radius: 8px;
  background: #14181e;
  isolation: isolate;
  box-shadow: 0 10px 26px rgba(17, 24, 39, 0.08);
}

.overview-video-surface {
  position: absolute;
  inset: 0;
  z-index: 0;
  display: block;
  width: 100%;
  height: 100%;
  object-fit: cover;
  opacity: 0;
  transition: opacity 220ms ease;
}

.overview-video-preview.has-frame .overview-video-surface { opacity: 1; }

.overview-video-frame {
  position: absolute;
  inset: 12px;
  z-index: 1;
  border: 1px solid rgba(255, 255, 255, 0.14);
  pointer-events: none;
}

.overview-video-hit {
  position: absolute;
  inset: 0;
  z-index: 2;
  display: block;
  width: 100%;
  height: 100%;
  border: 0;
  background: transparent;
  cursor: pointer;
}

.overview-video-hit:focus-visible {
  outline: 2px solid #60a5fa;
  outline-offset: -3px;
}

.overview-video-head,
.overview-video-foot {
  position: absolute;
  right: 16px;
  left: 16px;
  z-index: 3;
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 10px;
  pointer-events: none;
}

.overview-video-head { top: 16px; }
.overview-video-foot { bottom: 14px; color: rgba(255, 255, 255, 0.7); font-size: 8px; }
.overview-video-foot span { overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
.overview-video-foot span:last-child { max-width: 38%; text-align: right; }

.overview-video-kicker,
.overview-video-status {
  display: inline-flex;
  align-items: center;
  gap: 6px;
  color: rgba(255, 255, 255, 0.84);
  font-family: ui-monospace, "Cascadia Mono", Consolas, monospace;
  font-size: 8px;
  letter-spacing: 0.06em;
}

.overview-video-status { color: rgba(255, 255, 255, 0.68); }
.overview-video-status i { width: 6px; height: 6px; border-radius: 50%; background: #94a3b8; }
.overview-video-status.is-ok i { background: #34d399; box-shadow: 0 0 0 3px rgba(52, 211, 153, 0.14); }
.overview-video-status.is-connecting i { background: #f59e0b; animation: overview-video-breathe 1.4s ease-in-out infinite; }
.overview-video-status.is-held i { background: #f59e0b; }

.overview-video-empty {
  position: absolute;
  inset: 0;
  z-index: 1;
  display: grid;
  place-content: center;
  justify-items: center;
  gap: 8px;
  padding: 30px;
  color: rgba(255, 255, 255, 0.68);
  text-align: center;
  pointer-events: none;
}

.overview-video-empty svg { color: #93c5fd; }
.overview-video-empty strong { max-width: min(320px, 90%); color: rgba(255, 255, 255, 0.9); font-size: 12px; font-weight: 600; }
.overview-video-empty small { color: rgba(255, 255, 255, 0.5); font-size: 9px; }

.overview-video-reload {
  position: absolute;
  top: 12px;
  right: 12px;
  z-index: 4;
  display: inline-flex;
  align-items: center;
  justify-content: center;
  width: 28px;
  height: 28px;
  border: 1px solid rgba(255, 255, 255, 0.2);
  border-radius: 6px;
  background: rgba(9, 13, 19, 0.52);
  color: rgba(255, 255, 255, 0.82);
  cursor: pointer;
  transition: background-color 160ms ease, border-color 160ms ease, color 160ms ease;
}

.overview-video-reload:hover,
.overview-video-reload:focus-visible {
  border-color: rgba(147, 197, 253, 0.7);
  background: rgba(30, 64, 175, 0.62);
  color: #fff;
}

.overview-video-reload:disabled { cursor: wait; opacity: 0.65; }
.overview-video-open { position: absolute; right: 14px; bottom: 38px; z-index: 3; display: inline-flex; color: rgba(255, 255, 255, 0.68); pointer-events: none; transform: translate(2px, 2px); opacity: 0; transition: opacity 160ms ease, transform 160ms ease; }
.overview-video-preview:hover .overview-video-open, .overview-video-preview:focus-within .overview-video-open { opacity: 1; transform: translate(0, 0); }
.overview-video-spin { animation: overview-video-spin 0.8s linear infinite; }

@keyframes overview-video-spin { to { transform: rotate(360deg); } }
@keyframes overview-video-breathe { 0%, 100% { opacity: 1; } 50% { opacity: 0.35; } }

@media (max-width: 720px) {
  .overview-video-preview { aspect-ratio: 16 / 10; }
  .overview-video-head, .overview-video-foot { right: 11px; left: 11px; }
  .overview-video-head { top: 11px; }
  .overview-video-foot { bottom: 10px; font-size: 7px; }
  .overview-video-foot span:last-child { display: none; }
  .overview-video-frame { inset: 9px; }
  .overview-video-reload { top: 9px; right: 9px; }
}

@media (prefers-reduced-motion: reduce) {
  .overview-video-surface, .overview-video-open, .overview-video-reload { transition: none; }
  .overview-video-status.is-connecting i, .overview-video-spin { animation: none; }
}
</style>
