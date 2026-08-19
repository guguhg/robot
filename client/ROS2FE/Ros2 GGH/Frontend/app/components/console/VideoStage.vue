<script setup lang="ts">
import {
  CWebApiError,
  drawBgr8ToCanvas,
  type CameraImageFrame,
  type DeviceInfo,
} from '@/lib/cwebapi/cwebapi-client'
import { useLanCamera } from '@/composables/useLanCamera'
import type { LinkState } from '@/composables/useRobot'
import { createWhepReceiver } from '@/lib/whep-player'

type VideoSource = 'whep' | 'rosbridge' | 'lan'

interface VideoDiagnostics {
  state: LinkState
  source: VideoSource
  sourceMeta: string
  fps: number | null
  frameHeld: boolean
  error: string
}

const props = withDefaults(defineProps<{
  cameraImage?: CameraImageFrame | null
  cameraState?: LinkState
}>(), {
  cameraImage: null,
  cameraState: 'off',
})
const emit = defineEmits<{
  (event: 'state', value: LinkState): void
  (event: 'diagnostics', value: VideoDiagnostics): void
}>()
const { session, hasPermission, createAuthenticatedClient } = useAuth()
const runtimeConfig = useRuntimeConfig()
const apiBase = String(runtimeConfig.public.apiBase || '')
const srsBaseUrl = String((runtimeConfig.public as { srsBaseUrl?: string }).srsBaseUrl || '')

const videoEl = ref<HTMLVideoElement | null>(null)
const cameraCanvasEl = ref<HTMLCanvasElement | null>(null)
const devices = ref<DeviceInfo[]>([])
const selectedDeviceId = ref('')
const sourceMode = useState<VideoSource>('console-video-source', () => 'whep')
const whepState = ref<LinkState>('off')
const rosbridgeFrameActive = ref(false)
const lanFrameActive = ref(false)
const errorMessage = ref('')
const loading = ref(false)
const whepEndpoint = ref('')
const frameRate = ref<number | null>(null)
const whepFrameHeld = ref(false)
const whepHasPlayed = ref(false)
const rosbridgeFrameHeld = ref(false)
const lanFrameHeld = ref(false)
const frameWidth = ref(0)
const frameHeight = ref(0)
let stageActive = true
let stageNeedsResume = false

const selectedDevice = computed(() => devices.value.find(device => device.deviceId === selectedDeviceId.value) ?? null)
const cameraActive = computed(() => (
  sourceMode.value === 'rosbridge' ? rosbridgeFrameActive.value : sourceMode.value === 'lan' && lanFrameActive.value
))
const cameraSurfaceVisible = computed(() => (
  sourceMode.value === 'rosbridge'
    ? rosbridgeFrameActive.value || rosbridgeFrameHeld.value
    : sourceMode.value === 'lan' && (lanFrameActive.value || lanFrameHeld.value)
))
const isFrameHeld = computed(() => (
  sourceMode.value === 'whep'
    ? whepFrameHeld.value
    : sourceMode.value === 'rosbridge'
      ? rosbridgeFrameHeld.value
      : lanFrameHeld.value
))
const hasVisualFrame = computed(() => (
  sourceMode.value === 'whep' ? whepHasPlayed.value : cameraSurfaceVisible.value
))
const lanCamera = useLanCamera({
  onFrame: (frame) => {
    if (!stageActive || sourceMode.value !== 'lan') return
    if (!cameraCanvasEl.value) throw new Error('局域网视频画布尚未就绪。')
    drawBgr8ToCanvas(frame, cameraCanvasEl.value)
    lanFrameActive.value = true
    lanFrameHeld.value = false
    recordFrameSize(frame.width, frame.height)
    countVideoFrame()
    errorMessage.value = ''
    clearTimeout(lanStaleTimer)
    lanStaleTimer = setTimeout(() => {
      lanFrameActive.value = false
      lanFrameHeld.value = true
      resetFrameRate()
      errorMessage.value = ''
    }, 2500)
  },
})
const lanState = lanCamera.state
const lanError = lanCamera.error

const state = computed<LinkState>(() => {
  if (sourceMode.value === 'lan') {
    if (lanFrameActive.value) return 'ok'
    if (lanFrameHeld.value) return 'connecting'
    return lanState.value === 'connecting' ? 'connecting' : 'off'
  }
  if (sourceMode.value === 'rosbridge') {
    if (rosbridgeFrameActive.value) return 'ok'
    if (rosbridgeFrameHeld.value) return 'connecting'
    return props.cameraState === 'connecting' ? 'connecting' : 'off'
  }
  if (whepFrameHeld.value) return 'connecting'
  return whepState.value
})
const videoStateTone = computed(() => isFrameHeld.value ? 'held' : state.value)
const stateLabel = computed(() => {
  if (isFrameHeld.value) return 'FRAME HOLD'
  return state.value === 'ok' ? 'LIVE' : state.value === 'connecting' ? 'LINKING' : 'OFF'
})
const videoStatusTitle = computed(() => {
  if (sourceMode.value === 'lan') {
    if (lanFrameHeld.value) return '局域网摄像头帧已中断，当前保留最后画面'
    if (lanFrameActive.value) return '机器人局域网视频已连接'
    if (lanState.value === 'connecting') return '正在直连机器人局域网摄像头'
    return lanError.value || errorMessage.value || '局域网摄像头未连接'
  }
  if (sourceMode.value === 'rosbridge') {
    if (rosbridgeFrameHeld.value) return 'Rosbridge CameraImage 帧已中断，当前保留最后画面'
    if (rosbridgeFrameActive.value) return 'Rosbridge CameraImage 中转画面已连接'
    if (props.cameraState === 'connecting') return '正在连接 Rosbridge CameraImage 中转'
    return errorMessage.value || '尚未收到 Rosbridge CameraImage'
  }
  if (whepFrameHeld.value) return 'SRS WebRTC 视频帧已中断，当前保留最后画面'
  if (whepState.value === 'ok') return 'SRS WebRTC 视频已连接'
  if (whepState.value === 'connecting') return '正在连接 SRS WebRTC 视频'
  return errorMessage.value || 'SRS WebRTC 视频未连接'
})
const sourceMeta = computed(() => {
  if (sourceMode.value === 'lan') return `${lanCamera.url} · ${lanCamera.topic}`
  if (sourceMode.value === 'rosbridge') return 'MapHub · CameraImage'
  return whepEndpoint.value || '等待 SRS 播放地址'
})
const videoSourceLabel = computed(() => {
  if (sourceMode.value === 'lan') return 'LAN DIRECT'
  if (sourceMode.value === 'rosbridge') return 'CAMERA RELAY'
  return 'SRS WHEP'
})
const frameSizeLabel = computed(() => (
  frameWidth.value > 0 && frameHeight.value > 0
    ? `${frameWidth.value}×${frameHeight.value}`
    : 'NO FRAME'
))
const videoReadoutMeta = computed(() => (
  isFrameHeld.value ? `${frameSizeLabel.value} · LAST FRAME` : frameSizeLabel.value
))
const activeError = computed(() => sourceMode.value === 'lan' ? lanError.value || errorMessage.value : errorMessage.value)
const reloadTitle = computed(() => {
  if (sourceMode.value === 'lan') return '重新连接局域网摄像头'
  if (sourceMode.value === 'rosbridge') return '重新检测 Rosbridge 图像'
  return '重新连接 SRS WebRTC'
})

let player: ReturnType<typeof createWhepReceiver> | null = null
let requestVersion = 0
let stopSessionWatch: (() => void) | undefined
let stopCameraWatch: (() => void) | undefined
let whepMediaTimer: ReturnType<typeof setTimeout> | undefined
let whepStaleTimer: ReturnType<typeof setTimeout> | undefined
let rosbridgeStaleTimer: ReturnType<typeof setTimeout> | undefined
let lanStaleTimer: ReturnType<typeof setTimeout> | undefined
let frameRateTimer: ReturnType<typeof setInterval> | undefined
let videoFrameCallbackId: number | undefined
let frameWindowStarted = 0
let frameWindowCount = 0

watch(state, value => emit('state', value), { immediate: true })
watch([state, sourceMode, sourceMeta, frameRate, isFrameHeld, activeError], ([nextState, source, meta, fps, held, error]) => {
  emit('diagnostics', {
    state: nextState,
    source,
    sourceMeta: meta,
    fps,
    frameHeld: held,
    error,
  })
}, { immediate: true })

function stopPlayer() {
  clearTimeout(whepMediaTimer)
  whepMediaTimer = undefined
  clearTimeout(whepStaleTimer)
  whepStaleTimer = undefined
  whepFrameHeld.value = false
  whepHasPlayed.value = false
  stopFrameRateSampler()
  const currentPlayer = player
  player = null
  currentPlayer?.stop()
  whepEndpoint.value = ''
  resetFrameSize()
}

function resetFrameRate() {
  frameRate.value = null
  frameWindowStarted = 0
  frameWindowCount = 0
}

function resetFrameSize() {
  frameWidth.value = 0
  frameHeight.value = 0
}

function recordFrameSize(width: number, height: number) {
  if (!Number.isFinite(width) || !Number.isFinite(height) || width <= 0 || height <= 0) return
  frameWidth.value = Math.floor(width)
  frameHeight.value = Math.floor(height)
}

function countVideoFrame() {
  const now = performance.now()
  if (!frameWindowStarted) frameWindowStarted = now
  frameWindowCount += 1
  const elapsed = now - frameWindowStarted
  if (elapsed < 500) return
  frameRate.value = Math.min(240, Math.max(0, Math.round(frameWindowCount * 1000 / elapsed)))
  frameWindowStarted = now
  frameWindowCount = 0
}

function markWhepFrameArrived(version: number) {
  if (!stageActive || sourceMode.value !== 'whep' || version !== requestVersion || !player) return
  whepHasPlayed.value = true
  whepFrameHeld.value = false
  whepState.value = 'ok'
  errorMessage.value = ''
  const video = videoEl.value
  if (video) recordFrameSize(video.videoWidth, video.videoHeight)
  clearTimeout(whepStaleTimer)
  whepStaleTimer = setTimeout(() => {
    if (version !== requestVersion || sourceMode.value !== 'whep' || !player || !whepHasPlayed.value) return
    whepFrameHeld.value = true
    whepState.value = 'connecting'
    resetFrameRate()
    errorMessage.value = ''
  }, 2500)
}

function stopFrameRateSampler() {
  const video = videoEl.value as (HTMLVideoElement & {
    cancelVideoFrameCallback?: (handle: number) => void
  }) | null
  if (video && videoFrameCallbackId !== undefined) {
    video.cancelVideoFrameCallback?.(videoFrameCallbackId)
  }
  videoFrameCallbackId = undefined
  clearInterval(frameRateTimer)
  frameRateTimer = undefined
  resetFrameRate()
}

function startFrameRateSampler(version: number) {
  stopFrameRateSampler()
  if (!stageActive) return
  const video = videoEl.value as (HTMLVideoElement & {
    requestVideoFrameCallback?: (callback: (now: number, metadata: unknown) => void) => number
    webkitDecodedFrameCount?: number
  }) | null
  if (!video) return

  const isCurrentWhepAttempt = () => (
    version === requestVersion && sourceMode.value === 'whep' && player !== null
  )

  if (typeof video.requestVideoFrameCallback === 'function') {
    const onVideoFrame = () => {
      if (!isCurrentWhepAttempt()) return
      markWhepFrameArrived(version)
      countVideoFrame()
      videoFrameCallbackId = video.requestVideoFrameCallback!(onVideoFrame)
    }
    videoFrameCallbackId = video.requestVideoFrameCallback(onVideoFrame)
    return
  }

  const readDecodedFrameCount = () => {
    const quality = video.getVideoPlaybackQuality?.()
    const total = quality?.totalVideoFrames ?? video.webkitDecodedFrameCount
    return Number.isFinite(total) ? Number(total) : null
  }
  const initialFrameCount = readDecodedFrameCount()
  let previousSampleTime = performance.now()
  if (initialFrameCount === null) return
  let previousFrameCount: number = initialFrameCount

  frameRateTimer = setInterval(() => {
    if (!isCurrentWhepAttempt() || video.readyState < 2 || video.paused) return
    const total = readDecodedFrameCount()
    const now = performance.now()
    if (total === null) {
      previousSampleTime = now
      return
    }
    if (total < previousFrameCount) {
      previousFrameCount = total
      previousSampleTime = now
      return
    }
    const elapsed = now - previousSampleTime
    if (elapsed < 400) return
    frameRate.value = Math.min(240, Math.max(0, Math.round((total - previousFrameCount) * 1000 / elapsed)))
    if (total > previousFrameCount) markWhepFrameArrived(version)
    previousFrameCount = total
    previousSampleTime = now
  }, 500)
}

function handleVideoPlaying() {
  if (!stageActive || sourceMode.value !== 'whep' || !player) return
  const version = requestVersion
  clearTimeout(whepMediaTimer)
  whepMediaTimer = undefined
  whepHasPlayed.value = true
  whepFrameHeld.value = false
  markWhepFrameArrived(version)
  whepState.value = 'ok'
  errorMessage.value = ''
  startFrameRateSampler(version)
}

function armWhepMediaTimeout(version: number) {
  clearTimeout(whepMediaTimer)
  whepMediaTimer = setTimeout(() => {
    if (version !== requestVersion || sourceMode.value !== 'whep') return
    const video = videoEl.value
    if (video && video.readyState >= 2 && video.videoWidth > 0 && video.videoHeight > 0) {
      handleVideoPlaying()
      return
    }
    if (whepHasPlayed.value) {
      whepFrameHeld.value = true
      whepState.value = 'connecting'
      resetFrameRate()
      errorMessage.value = ''
      return
    }
    whepState.value = 'off'
    errorMessage.value = 'SRS WebRTC 已连通，但当前没有可播放的视频帧。'
  }, 6000)
}

function handleVideoWaiting() {
  if (!stageActive || sourceMode.value !== 'whep' || !player) return
  if (!whepHasPlayed.value) {
    whepState.value = 'connecting'
    armWhepMediaTimeout(requestVersion)
  }
}

function stopRosbridgeCamera() {
  clearTimeout(rosbridgeStaleTimer)
  rosbridgeStaleTimer = undefined
  rosbridgeFrameActive.value = false
  rosbridgeFrameHeld.value = false
}

function stopLanCamera() {
  clearTimeout(lanStaleTimer)
  lanStaleTimer = undefined
  lanFrameActive.value = false
  lanFrameHeld.value = false
  lanCamera.stop()
}

function drawRosbridgeFrame(frame: CameraImageFrame | null) {
  if (!stageActive || sourceMode.value !== 'rosbridge' || !frame || !session.value || !cameraCanvasEl.value) return
  try {
    drawBgr8ToCanvas(frame, cameraCanvasEl.value)
    rosbridgeFrameActive.value = true
    rosbridgeFrameHeld.value = false
    recordFrameSize(frame.width, frame.height)
    countVideoFrame()
    errorMessage.value = ''
    clearTimeout(rosbridgeStaleTimer)
    rosbridgeStaleTimer = setTimeout(() => {
      rosbridgeFrameActive.value = false
      rosbridgeFrameHeld.value = true
      resetFrameRate()
      errorMessage.value = ''
    }, 2500)
  } catch {
    stopRosbridgeCamera()
    errorMessage.value = 'Rosbridge CameraImage 图像解码失败。'
  }
}

function describeError(error: unknown) {
  if (!(error instanceof CWebApiError)) {
    if (error instanceof TypeError && /fetch/i.test(error.message)) return '无法访问内网 SRS WHEP 服务。'
    return error instanceof Error ? error.message : '视频连接失败'
  }
  if (error.status === 400 || error.status === 404) return 'SRS 当前没有该设备的活动推流。'
  if (error.status === 403) return '当前账户没有视频播放权限。'
  if (error.status === 503) return '视频服务暂不可用。'
  if (error.status === 0) return '无法连接视频服务。'
  return error.message || '视频连接失败'
}

function handlePeerState(next: RTCPeerConnectionState) {
  if (sourceMode.value !== 'whep') return
  if (next === 'connected') {
    const video = videoEl.value
    if (video && video.readyState >= 2 && video.videoWidth > 0 && video.videoHeight > 0) handleVideoPlaying()
    else whepState.value = 'connecting'
    return
  }
  if (next === 'new' || next === 'connecting') {
    whepState.value = 'connecting'
    return
  }
  if (next === 'failed' || next === 'disconnected' || next === 'closed') {
    stopFrameRateSampler()
    clearTimeout(whepStaleTimer)
    whepStaleTimer = undefined
    whepFrameHeld.value = false
    whepHasPlayed.value = false
    whepState.value = 'off'
    if (next !== 'closed') errorMessage.value = 'SRS WebRTC 连接已中断。'
  }
}

async function startSelected(version = ++requestVersion) {
  stopPlayer()
  whepState.value = 'off'
  errorMessage.value = ''

  if (!session.value) {
    errorMessage.value = '请先登录。'
    return
  }
  if (!hasPermission('stream.play')) {
    errorMessage.value = session.value.isGuest ? '游客账户不支持视频播放。' : '当前账户没有视频播放权限。'
    return
  }
  if (!selectedDevice.value) {
    errorMessage.value = '暂无可用设备。'
    return
  }
  if (!videoEl.value) return

  whepState.value = 'connecting'
  const playerRef = { value: null as ReturnType<typeof createWhepReceiver> | null }
  const nextPlayer = createWhepReceiver(createAuthenticatedClient(), videoEl.value, {
    apiBase,
    srsBaseUrl,
    onStateChange: (nextState) => {
      if (version !== requestVersion || player !== playerRef.value) return
      handlePeerState(nextState)
    },
    onResolvedUrl: (url) => {
      if (version !== requestVersion || player !== playerRef.value) return
      whepEndpoint.value = new URL(url).origin
    },
  })
  playerRef.value = nextPlayer
  player = nextPlayer

  try {
    const result = await nextPlayer.start(selectedDevice.value.deviceId)
    if (version !== requestVersion || player !== nextPlayer) {
      if (player === nextPlayer) {
        player = null
        nextPlayer.stop()
      }
      return
    }
    whepEndpoint.value = new URL(result.resolvedWhepUrl).origin
    armWhepMediaTimeout(version)
  } catch (error) {
    if (version !== requestVersion || player !== nextPlayer) return
    whepState.value = 'off'
    errorMessage.value = describeError(error)
    player = null
    nextPlayer.stop()
  }
}

async function activateSource(mode: VideoSource, version = ++requestVersion) {
  stopPlayer()
  stopRosbridgeCamera()
  stopLanCamera()
  loading.value = false
  devices.value = []
  selectedDeviceId.value = ''
  errorMessage.value = ''
  whepState.value = 'off'

  if (!session.value) {
    errorMessage.value = '请先登录。'
    return
  }
  if (!hasPermission('stream.play')) {
    errorMessage.value = session.value.isGuest ? '游客账户不支持视频播放。' : '当前账户没有视频播放权限。'
    return
  }

  if (mode === 'rosbridge') {
    drawRosbridgeFrame(props.cameraImage)
    if (!rosbridgeFrameActive.value) errorMessage.value = '等待 Rosbridge CameraImage 图像。'
    return
  }
  if (mode === 'lan') {
    lanCamera.start()
    return
  }

  loading.value = true
  whepState.value = 'connecting'
  try {
    const list = await createAuthenticatedClient().getDevices({ signal: AbortSignal.timeout(8000) })
    if (version !== requestVersion) return
    devices.value = list
    selectedDeviceId.value = list.find(device => device.isOnline)?.deviceId ?? list[0]?.deviceId ?? ''
    await nextTick()
    await startSelected(version)
  } catch (error) {
    if (version !== requestVersion) return
    whepState.value = 'off'
    errorMessage.value = describeError(error)
  } finally {
    if (version === requestVersion) loading.value = false
  }
}

function switchSource(mode: VideoSource) {
  if (mode === sourceMode.value) return
  sourceMode.value = mode
  void activateSource(mode)
}

function reloadSource() {
  void activateSource(sourceMode.value)
}

function changeDevice() {
  if (sourceMode.value !== 'whep') return
  const version = ++requestVersion
  stopPlayer()
  void startSelected(version)
}

onMounted(() => {
  stopSessionWatch = watch(
    () => session.value?.token,
    () => void activateSource(sourceMode.value),
    { immediate: true },
  )
  stopCameraWatch = watch(
    () => props.cameraImage,
    frame => drawRosbridgeFrame(frame),
    { immediate: true, flush: 'post' },
  )
})

onActivated(() => {
  if (!stageNeedsResume) return
  stageActive = true
  stageNeedsResume = false
  if (sourceMode.value === 'whep' && player) {
    const video = videoEl.value
    if (video) void video.play().catch(() => {})
    return
  }
  if (sourceMode.value === 'lan') {
    lanCamera.start()
    return
  }
  if (sourceMode.value === 'rosbridge') {
    drawRosbridgeFrame(props.cameraImage)
    return
  }
  if (session.value) void activateSource(sourceMode.value)
})

onDeactivated(() => {
  stageActive = false
  stageNeedsResume = true
  /* 概览拥有独立的轻量视频预览；离开控制台时释放不可见的 WHEP 接收器，避免双重 RTC。 */
  requestVersion += 1
  stopPlayer()
  whepState.value = 'off'
  stopFrameRateSampler()
  clearTimeout(whepMediaTimer)
  whepMediaTimer = undefined
  clearTimeout(whepStaleTimer)
  whepStaleTimer = undefined
  videoEl.value?.pause()
  if (sourceMode.value === 'lan') stopLanCamera()
})

onUnmounted(() => {
  stageActive = false
  stageNeedsResume = false
  requestVersion += 1
  stopSessionWatch?.()
  stopCameraWatch?.()
  stopPlayer()
  stopRosbridgeCamera()
  stopLanCamera()
  whepState.value = 'off'
  emit('state', 'off')
  emit('diagnostics', {
    state: 'off',
    source: sourceMode.value,
    sourceMeta: sourceMeta.value,
    fps: null,
    frameHeld: false,
    error: activeError.value,
  })
})
</script>

<template>
  <section
    class="video-stage"
    :class="{ 'is-live': state === 'ok', 'has-frame': hasVisualFrame }"
    :aria-label="videoStatusTitle"
  >
    <video
      v-show="sourceMode === 'whep'"
      ref="videoEl"
      class="video-surface"
      autoplay
      muted
      playsinline
      @playing="handleVideoPlaying"
      @waiting="handleVideoWaiting"
    ></video>
    <canvas
      v-show="cameraSurfaceVisible"
      ref="cameraCanvasEl"
      class="video-surface camera-surface"
      aria-hidden="true"
    ></canvas>

    <div
      class="video-readout hud-mono"
      :class="{ 'is-held': isFrameHeld }"
      aria-live="polite"
    >
      <span class="video-readout-dot" :class="`is-${videoStateTone}`" aria-hidden="true"></span>
      <span class="video-readout-kicker">{{ videoSourceLabel }}</span>
      <span class="video-readout-meta">{{ videoReadoutMeta }}</span>
      <span class="video-readout-status">{{ stateLabel }}</span>
    </div>

    <div v-if="frameRate !== null" class="video-fps hud-mono" aria-live="polite">
      <span class="video-fps-dot" aria-hidden="true"></span>
      <span>FPS {{ frameRate }}</span>
    </div>

    <Teleport defer to="#media-drawer-content">
      <section class="media-toolbar video-toolbar" aria-label="视频设置">
        <div class="media-toolbar-head">
          <div class="media-toolbar-heading">
            <span class="media-status-dot" :class="`is-${videoStateTone}`" :title="videoStatusTitle" aria-hidden="true"></span>
            <span class="media-toolbar-title hud-mono">VIDEO</span>
            <span class="media-toolbar-state hud-mono">{{ stateLabel }}</span>
          </div>
          <button type="button" :title="reloadTitle" :aria-label="reloadTitle" :disabled="loading" @click="reloadSource">
            <Icon :class="{ 'media-spin': loading || state === 'connecting' }" name="lucide:refresh-cw" size="15" />
          </button>
        </div>

        <div class="video-source-switcher" role="group" aria-label="视频来源">
          <button
            type="button"
            class="video-source-button"
            :class="{ active: sourceMode === 'whep' }"
            :aria-pressed="sourceMode === 'whep'"
            title="SRS WebRTC"
            @click="switchSource('whep')"
          >
            <Icon name="lucide:radio-tower" size="13" />
            <span>SRS WebRTC</span>
          </button>
          <button
            type="button"
            class="video-source-button"
            :class="{ active: sourceMode === 'rosbridge' }"
            :aria-pressed="sourceMode === 'rosbridge'"
            title="后端 MapHub / CameraImage（ROS 经 rosbridge 中转）"
            @click="switchSource('rosbridge')"
          >
            <Icon name="lucide:network" size="13" />
            <span>Rosbridge</span>
          </button>
          <button
            type="button"
            class="video-source-button"
            :class="{ active: sourceMode === 'lan' }"
            :aria-pressed="sourceMode === 'lan'"
            title="浏览器直连机器人局域网摄像头"
            @click="switchSource('lan')"
          >
            <Icon name="lucide:router" size="13" />
            <span>局域网</span>
          </button>
        </div>

        <label v-if="sourceMode === 'whep'" class="media-field">
          <span class="media-field-label">设备</span>
          <select v-model="selectedDeviceId" :disabled="loading || devices.length === 0" @change="changeDevice">
            <option v-if="devices.length === 0" value="">无设备</option>
            <option v-for="device in devices" :key="device.deviceId" :value="device.deviceId">
              {{ device.name }} · {{ device.isOnline ? '在线' : '待探测' }}
            </option>
          </select>
        </label>

        <div class="media-source-meta hud-mono" :title="sourceMeta">
          <Icon name="lucide:waypoints" size="12" />
          <span>{{ sourceMeta }}</span>
        </div>
      </section>
    </Teleport>

    <div v-if="activeError && !cameraActive" class="media-empty" role="status">
      <Icon name="lucide:video-off" size="22" />
      <strong>{{ activeError }}</strong>
      <span v-if="sourceMode === 'whep' && selectedDevice">{{ selectedDevice.name }}</span>
    </div>
    <div v-else-if="state === 'connecting' && !isFrameHeld" class="media-empty" role="status">
      <Icon class="media-spin" name="lucide:loader-circle" size="22" />
      <strong>正在连接视频</strong>
    </div>
    <div v-if="isFrameHeld" class="video-frame-hold hud-mono" role="status">
      <Icon name="lucide:pause" size="12" />
      <span>视频帧暂未更新</span>
      <small>保留最后画面</small>
    </div>
  </section>
</template>

<style>
.video-stage {
  position: absolute;
  inset: 0;
  overflow: hidden;
  background: #fff;
  transition: background-color 0.18s ease;
}

.video-stage.has-frame {
  background: #111318;
}

.video-surface {
  width: 100%;
  height: 100%;
  display: block;
  object-fit: contain;
  background: transparent;
}

.camera-surface {
  image-rendering: auto;
}

.video-readout {
  position: absolute;
  top: 18px;
  left: 18px;
  z-index: 2;
  display: inline-flex;
  align-items: center;
  gap: 7px;
  max-width: min(420px, calc(100vw - 150px));
  padding: 6px 9px 6px 8px;
  border-top: 1px solid rgba(13, 148, 136, 0.48);
  border-left: 1px solid rgba(13, 148, 136, 0.58);
  background: rgba(250, 253, 251, 0.78);
  box-shadow: 8px 8px 20px rgba(43, 91, 82, 0.06);
  color: #52686a;
  font-size: 8.5px;
  letter-spacing: 0.06em;
  pointer-events: none;
  transition:
    border-color 180ms ease,
    background-color 180ms ease,
    color 180ms ease;
}

.video-stage.has-frame .video-readout {
  border-top-color: rgba(255, 255, 255, 0.48);
  border-left-color: rgba(255, 255, 255, 0.58);
  background: rgba(8, 10, 14, 0.46);
  box-shadow: 8px 8px 20px rgba(0, 0, 0, 0.16);
  color: rgba(255, 255, 255, 0.72);
}

.video-readout.is-held {
  border-top-color: rgba(217, 119, 6, 0.58);
  border-left-color: rgba(217, 119, 6, 0.68);
  background: rgba(91, 54, 12, 0.56);
  color: rgba(255, 239, 196, 0.88);
}

.video-readout-dot {
  width: 5px;
  height: 5px;
  flex: none;
  border-radius: 50%;
  background: #94a3b8;
  box-shadow: 0 0 0 4px rgba(148, 163, 184, 0.12);
}

.video-readout-dot.is-ok {
  background: #22c55e;
  box-shadow: 0 0 0 4px rgba(34, 197, 94, 0.14);
}

.video-readout-dot.is-connecting,
.video-readout-dot.is-held {
  background: #f59e0b;
  box-shadow: 0 0 0 4px rgba(245, 158, 11, 0.14);
}

.video-readout-kicker {
  color: #0f766e;
  font-size: 8px;
  white-space: nowrap;
}

.video-stage.has-frame .video-readout-kicker {
  color: rgba(255, 255, 255, 0.9);
}

.video-readout.is-held .video-readout-kicker,
.video-readout.is-held .video-readout-status {
  color: #fbbf24;
}

.video-readout-meta {
  flex: 1 1 auto;
  min-width: 0;
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}

.video-readout-status {
  color: #64748b;
  font-size: 7.5px;
  white-space: nowrap;
}

.video-stage.has-frame .video-readout-status {
  color: rgba(255, 255, 255, 0.64);
}

.video-fps {
  position: absolute;
  left: 18px;
  bottom: clamp(96px, 15vh, 124px);
  z-index: 2;
  display: inline-flex;
  align-items: center;
  gap: 7px;
  padding: 5px 8px;
  border-left: 1px solid rgba(255, 255, 255, 0.56);
  background: rgba(8, 10, 14, 0.48);
  color: rgba(255, 255, 255, 0.86);
  font-size: 9px;
  letter-spacing: 0.08em;
  pointer-events: none;
}

.video-fps-dot {
  width: 5px;
  height: 5px;
  border-radius: 50%;
  background: #22c55e;
  box-shadow: 0 0 0 4px rgba(34, 197, 94, 0.15);
}

.video-toolbar .media-status-dot.is-held {
  background: #f59e0b;
  box-shadow: 0 0 0 4px rgba(245, 158, 11, 0.14);
}

.video-source-switcher {
  display: grid;
  grid-template-columns: repeat(3, minmax(0, 1fr));
  gap: 4px;
}

.video-source-switcher .video-source-button {
  width: 100%;
  min-width: 0;
  gap: 3px;
  padding: 0 2px;
  border: 1px solid var(--border);
  color: var(--muted-foreground);
  font-size: 8.5px;
}

.video-source-switcher .video-source-button span {
  white-space: nowrap;
}

.video-source-switcher .video-source-button.active {
  border-color: color-mix(in srgb, var(--primary) 48%, var(--border));
  background: color-mix(in srgb, var(--primary) 12%, transparent);
  color: var(--primary);
}

.media-source-meta {
  display: flex;
  align-items: center;
  gap: 6px;
  min-width: 0;
  color: var(--muted-foreground);
  font-size: 8.5px;
}

.media-source-meta span {
  min-width: 0;
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}

.media-empty {
  position: absolute;
  inset: 0;
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  gap: 8px;
  padding: 80px 24px;
  color: var(--muted-foreground);
  text-align: center;
  pointer-events: none;
}

.video-frame-hold {
  position: absolute;
  top: 54px;
  left: 18px;
  z-index: 2;
  display: inline-flex;
  align-items: center;
  gap: 6px;
  padding: 5px 8px;
  border-left: 1px solid rgba(245, 158, 11, 0.78);
  background: rgba(91, 54, 12, 0.56);
  color: rgba(255, 239, 196, 0.92);
  font-size: 8px;
  letter-spacing: 0.04em;
  pointer-events: none;
}

.video-frame-hold small {
  padding-left: 6px;
  border-left: 1px solid rgba(255, 239, 196, 0.24);
  color: rgba(251, 191, 36, 0.9);
  font-size: 7px;
}

@media (max-width: 520px) {
  .video-fps {
    left: 12px;
    bottom: 112px;
  }

  .video-readout {
    top: 12px;
    left: 12px;
    gap: 5px;
    max-width: calc(100vw - 120px);
  }

  .video-frame-hold {
    top: 48px;
    left: 12px;
  }
}

.media-empty strong {
  max-width: min(360px, 80vw);
  color: var(--foreground);
  font-size: 13px;
  font-weight: 500;
  overflow-wrap: anywhere;
}

.media-empty span {
  font-size: 10px;
  color: var(--muted-foreground);
}
</style>
