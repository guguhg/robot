import {
  CWebApiError,
  type CameraImageFrame,
  type CostmapFrame,
  type MapFrame,
  type MapHubClient,
  type OdometryFrame,
  type RobotPose,
  type Velocity,
} from '@/lib/cwebapi/cwebapi-client'
import { useAuth, type AuthSession } from '@/composables/useAuth'
import type { LinkState } from '@/composables/useRobot'
import {
  clearMapSnapshot,
  createMapSnapshotScope,
  loadMapSnapshot,
  saveMapSnapshot,
} from '@/lib/map-snapshot-cache'

const MOTION_REFRESH_MS = 100
const MOTION_STALE_MS = 2500
const MAP_FRAME_RETRY_DELAY_MS = 1500
const MAP_SNAPSHOT_WRITE_MIN_INTERVAL_MS = 2000

export interface NavigationPose {
  x: number
  y: number
  theta: number
}

export type MapFrameSource = 'none' | 'cached' | 'live'

export type TelemetryMetricKey =
  | 'map'
  | 'robotPose'
  | 'particles'
  | 'path'
  | 'localPlan'
  | 'scan'
  | 'globalCostmap'
  | 'localCostmap'
  | 'mapPatch'

export const TELEMETRY_STALE_AFTER_MS: Readonly<Record<TelemetryMetricKey, number>> = {
  map: Infinity,
  robotPose: 2500,
  particles: 6000,
  path: 8000,
  localPlan: 4000,
  scan: 2500,
  globalCostmap: 12000,
  localCostmap: 5000,
  mapPatch: 15000,
}

export interface FrameMetric {
  lastAt: number
  hz: number
  count: number
}

interface SmoothingOptions {
  min: number
  max: number
  windowSize: number
  alpha: number
  deadband: number
  precision: number
}

function createTelemetryFilter(options: SmoothingOptions) {
  const samples: number[] = []
  let smoothed: number | null = null
  let displayed: number | null = null

  function reset() {
    samples.length = 0
    smoothed = null
    displayed = null
  }

  function push(raw: number) {
    if (!Number.isFinite(raw)) return null

    const value = Math.min(options.max, Math.max(options.min, raw))
    samples.push(value)
    if (samples.length > options.windowSize) samples.shift()

    const sorted = [...samples].sort((a, b) => a - b)
    const middle = Math.floor(sorted.length / 2)
    const median = sorted.length % 2 === 0
      ? (sorted[middle - 1]! + sorted[middle]!) / 2
      : sorted[middle]!

    smoothed = smoothed === null ? median : smoothed + options.alpha * (median - smoothed)
    if (displayed === null || Math.abs(smoothed - displayed) >= options.deadband) {
      const scale = 10 ** options.precision
      displayed = Math.round(smoothed * scale) / scale
    }
    return displayed
  }

  return { push, reset }
}

function describeTelemetryError(error: unknown) {
  if (!(error instanceof CWebApiError)) return error instanceof Error ? error.message : '遥测连接失败'
  if (error.status === 401) return '登录状态已失效。'
  if (error.status === 503) return '遥测服务暂不可用。'
  if (error.status === 0) return '无法连接遥测服务。'
  return error.message || '遥测连接失败'
}

function describeMapFrameIssue(error: unknown) {
  const message = error instanceof Error ? error.message : '未知错误'
  if (message === 'Invalid map magic byte') return '地图帧格式不匹配（期望标识 0x4D）。'
  return `地图帧解码失败：${message}`
}

function getMapFrameFingerprint(frame: MapFrame) {
  let hash = 2_166_136_261
  const geometry = [
    frame.width,
    frame.height,
    frame.resolution,
    frame.originX,
    frame.originY,
    frame.originYaw,
  ].join('|')
  for (let index = 0; index < geometry.length; index += 1) {
    hash = Math.imul(hash ^ geometry.charCodeAt(index), 16_777_619)
  }
  const cells = Math.min(frame.data.length, frame.width * frame.height)
  for (let index = 0; index < cells; index += 1) {
    hash = Math.imul(hash ^ (frame.data[index]! & 0xff), 16_777_619)
  }
  return `${geometry}|${hash >>> 0}`
}

export function useTelemetry() {
  const { session, createAuthenticatedClient } = useAuth()
  const runtimeConfig = useRuntimeConfig()

  const mapState = ref<LinkState>('off')
  const batteryPercent = ref<number | null>(null)
  const batteryVoltage = ref<number | null>(null)
  const cameraImage = shallowRef<CameraImageFrame | null>(null)
  const mapFrame = shallowRef<MapFrame | null>(null)
  const mapFrameSource = ref<MapFrameSource>('none')
  const mapFrameCachedAt = ref(0)
  const robotPose = shallowRef<RobotPose | null>(null)
  const particles = shallowRef<Float32Array | null>(null)
  const globalPath = shallowRef<Float32Array | null>(null)
  const localPlan = shallowRef<Float32Array | null>(null)
  const scan = shallowRef<Float32Array | null>(null)
  const globalCostmap = shallowRef<CostmapFrame | null>(null)
  const localCostmap = shallowRef<CostmapFrame | null>(null)
  const mapPatch = shallowRef<CostmapFrame | null>(null)
  const odometry = shallowRef<OdometryFrame | null>(null)
  const safeTwist = shallowRef<Velocity | null>(null)
  const telemetryError = ref('')
  const mapFrameIssue = ref('')
  const telemetryMetrics = reactive<Record<TelemetryMetricKey, FrameMetric>>({
    map: { lastAt: 0, hz: 0, count: 0 },
    robotPose: { lastAt: 0, hz: 0, count: 0 },
    particles: { lastAt: 0, hz: 0, count: 0 },
    path: { lastAt: 0, hz: 0, count: 0 },
    localPlan: { lastAt: 0, hz: 0, count: 0 },
    scan: { lastAt: 0, hz: 0, count: 0 },
    globalCostmap: { lastAt: 0, hz: 0, count: 0 },
    localCostmap: { lastAt: 0, hz: 0, count: 0 },
    mapPatch: { lastAt: 0, hz: 0, count: 0 },
  })

  let hub: MapHubClient | null = null
  let generation = 0
  let stopSessionWatch: (() => void) | undefined
  let motionFlushTimer: ReturnType<typeof setTimeout> | undefined
  let odometryStaleTimer: ReturnType<typeof setTimeout> | undefined
  let safeTwistStaleTimer: ReturnType<typeof setTimeout> | undefined
  let mapFrameRetryTimer: ReturnType<typeof setTimeout> | undefined
  let pendingOdometry: OdometryFrame | undefined
  let pendingSafeTwist: Velocity | undefined
  let mapSnapshotScope = ''
  let mapSnapshotFingerprint = ''
  let mapSnapshotStorageVersion = 0
  let mapSnapshotQueue: Promise<void> = Promise.resolve()
  let mapSnapshotSaveTimer: ReturnType<typeof setTimeout> | undefined
  let mapSnapshotLastQueuedAt = 0
  let pendingMapSnapshot: {
    scope: string
    frame: MapFrame
    savedAt: number
    storageVersion: number
  } | null = null
  const socFilter = createTelemetryFilter({
    min: 0,
    max: 100,
    windowSize: 5,
    alpha: 0.2,
    deadband: 0.6,
    precision: 1,
  })
  const voltageFilter = createTelemetryFilter({
    min: 0,
    max: 1000,
    windowSize: 5,
    alpha: 0.25,
    deadband: 0.08,
    precision: 2,
  })

  function scheduleMotionFlush() {
    if (motionFlushTimer) return
    motionFlushTimer = setTimeout(() => {
      motionFlushTimer = undefined
      if (pendingOdometry) {
        odometry.value = pendingOdometry
        pendingOdometry = undefined
      }
      if (pendingSafeTwist) {
        safeTwist.value = pendingSafeTwist
        pendingSafeTwist = undefined
      }
    }, MOTION_REFRESH_MS)
  }

  function queueOdometry(value: OdometryFrame) {
    const fields = [value.x, value.y, value.theta, value.linearVelocity, value.angularVelocity]
    if (!fields.every(Number.isFinite)) return false

    pendingOdometry = { ...value }
    clearTimeout(odometryStaleTimer)
    odometryStaleTimer = setTimeout(() => {
      pendingOdometry = undefined
      odometry.value = null
    }, MOTION_STALE_MS)
    scheduleMotionFlush()
    return true
  }

  function queueSafeTwist(linear: number, angular: number) {
    if (!Number.isFinite(linear) || !Number.isFinite(angular)) return false

    pendingSafeTwist = { linearX: linear, angularZ: angular }
    clearTimeout(safeTwistStaleTimer)
    safeTwistStaleTimer = setTimeout(() => {
      pendingSafeTwist = undefined
      safeTwist.value = null
    }, MOTION_STALE_MS)
    scheduleMotionFlush()
    return true
  }

  function markTelemetryReady() {
    mapState.value = 'ok'
    telemetryError.value = ''
  }

  function markFrame(key: TelemetryMetricKey) {
    const metric = telemetryMetrics[key]
    const now = Date.now()
    if (metric.lastAt > 0) {
      const elapsed = now - metric.lastAt
      if (elapsed > 0) {
        const instantHz = Math.min(120, 1000 / elapsed)
        metric.hz = metric.count <= 1 ? instantHz : metric.hz * 0.72 + instantHz * 0.28
      }
    }
    metric.lastAt = now
    metric.count += 1
  }

  function resetTelemetryMetrics(preserveMap = false) {
    const cachedMapMetric = preserveMap ? { ...telemetryMetrics.map } : undefined
    for (const metric of Object.values(telemetryMetrics)) {
      metric.lastAt = 0
      metric.hz = 0
      metric.count = 0
    }
    if (cachedMapMetric) Object.assign(telemetryMetrics.map, cachedMapMetric)
  }

  function clearMapFrameRetry() {
    if (mapFrameRetryTimer === undefined) return
    clearTimeout(mapFrameRetryTimer)
    mapFrameRetryTimer = undefined
  }

  function hasLiveMapFrame() {
    return mapFrameSource.value === 'live'
  }

  function discardPendingMapSnapshot() {
    clearTimeout(mapSnapshotSaveTimer)
    mapSnapshotSaveTimer = undefined
    pendingMapSnapshot = null
  }

  function flushMapSnapshotSave() {
    mapSnapshotSaveTimer = undefined
    const pending = pendingMapSnapshot
    pendingMapSnapshot = null
    if (!pending) return
    mapSnapshotLastQueuedAt = Date.now()
    mapSnapshotQueue = mapSnapshotQueue
      .catch(() => {})
      .then(async () => {
        if (
          pending.storageVersion !== mapSnapshotStorageVersion
          || pending.scope !== mapSnapshotScope
        ) return
        await saveMapSnapshot(pending.scope, pending.frame, pending.savedAt)
      })
  }

  function queueMapSnapshotSave(scope: string, frame: MapFrame, savedAt: number) {
    const fingerprint = getMapFrameFingerprint(frame)
    if (!scope || fingerprint === mapSnapshotFingerprint) return
    mapSnapshotFingerprint = fingerprint
    pendingMapSnapshot = { scope, frame, savedAt, storageVersion: mapSnapshotStorageVersion }
    if (mapSnapshotSaveTimer !== undefined) return
    const elapsed = Date.now() - mapSnapshotLastQueuedAt
    const delay = Math.max(0, MAP_SNAPSHOT_WRITE_MIN_INTERVAL_MS - elapsed)
    if (delay === 0) {
      flushMapSnapshotSave()
      return
    }
    mapSnapshotSaveTimer = setTimeout(flushMapSnapshotSave, delay)
  }

  function queueMapSnapshotClear(scope: string) {
    if (!scope) return
    discardPendingMapSnapshot()
    mapSnapshotStorageVersion += 1
    mapSnapshotQueue = mapSnapshotQueue
      .catch(() => {})
      .then(async () => {
        await clearMapSnapshot(scope)
      })
  }

  async function restoreMapSnapshot(scope: string, currentGeneration: number) {
    const snapshot = await loadMapSnapshot(scope)
    if (
      !snapshot
      || currentGeneration !== generation
      || scope !== mapSnapshotScope
      || mapFrameSource.value === 'live'
    ) return

    mapFrame.value = snapshot.frame
    mapFrameSource.value = 'cached'
    mapFrameCachedAt.value = snapshot.savedAt
    const metric = telemetryMetrics.map
    metric.lastAt = snapshot.savedAt
    metric.hz = 0
    metric.count = 1
  }

  function resetTelemetryValues({ preserveMap = false }: { preserveMap?: boolean } = {}) {
    const keepMap = preserveMap && mapFrame.value !== null
    clearTimeout(motionFlushTimer)
    clearTimeout(odometryStaleTimer)
    clearTimeout(safeTwistStaleTimer)
    clearMapFrameRetry()
    motionFlushTimer = undefined
    odometryStaleTimer = undefined
    safeTwistStaleTimer = undefined
    pendingOdometry = undefined
    pendingSafeTwist = undefined
    socFilter.reset()
    voltageFilter.reset()
    batteryPercent.value = null
    batteryVoltage.value = null
    cameraImage.value = null
    mapFrameIssue.value = ''
    if (keepMap) {
      mapFrameSource.value = 'cached'
      if (!mapFrameCachedAt.value) mapFrameCachedAt.value = telemetryMetrics.map.lastAt || Date.now()
    } else {
      mapFrame.value = null
      mapFrameSource.value = 'none'
      mapFrameCachedAt.value = 0
    }
    robotPose.value = null
    particles.value = null
    globalPath.value = null
    localPlan.value = null
    scan.value = null
    globalCostmap.value = null
    localCostmap.value = null
    mapPatch.value = null
    odometry.value = null
    safeTwist.value = null
    resetTelemetryMetrics(keepMap)
  }

  async function retryMapFrame(next: MapHubClient, currentGeneration: number) {
    if (currentGeneration !== generation || next !== hub || hasLiveMapFrame() || mapState.value !== 'ok') return
    try {
      await next.subscribe()
    } catch (error) {
      if (currentGeneration !== generation || next !== hub || hasLiveMapFrame()) return
      mapFrameIssue.value = `地图缓存补订阅失败：${describeTelemetryError(error)}`
      return
    }
    if (currentGeneration !== generation || next !== hub || hasLiveMapFrame() || mapFrameIssue.value) return
    mapFrameIssue.value = mapFrame.value
      ? 'MapHub 已连接，但未收到新的 Map 地图帧；当前显示最近缓存。'
      : 'MapHub 已连接，但未收到 Map 地图帧。'
  }

  function scheduleMapFrameRetry(next: MapHubClient, currentGeneration: number) {
    clearMapFrameRetry()
    mapFrameRetryTimer = setTimeout(() => {
      mapFrameRetryTimer = undefined
      void retryMapFrame(next, currentGeneration)
    }, MAP_FRAME_RETRY_DELAY_MS)
  }

  function updateBatteryPercent(value: number) {
    const next = socFilter.push(value)
    if (next !== null) batteryPercent.value = next
  }

  function updateBatteryVoltage(value: number) {
    const next = voltageFilter.push(value)
    if (next !== null) batteryVoltage.value = next
  }

  async function stopHub() {
    const current = hub
    hub = null
    mapState.value = 'off'
    if (!current) return
    try {
      await current.stop()
    } catch {
      /* 连接已经关闭时无需重复报错。 */
    }
  }

  async function connectForSession(nextSession: AuthSession | null) {
    const currentGeneration = ++generation
    const previousSnapshotScope = mapSnapshotScope
    mapSnapshotStorageVersion += 1
    discardPendingMapSnapshot()
    await stopHub()
    if (currentGeneration !== generation) return

    resetTelemetryValues()
    telemetryError.value = ''
    if (!nextSession) {
      mapSnapshotScope = ''
      mapSnapshotFingerprint = ''
      queueMapSnapshotClear(previousSnapshotScope)
      return
    }

    const nextSnapshotScope = createMapSnapshotScope(runtimeConfig.public.apiBase, nextSession)
    mapSnapshotScope = nextSnapshotScope
    mapSnapshotFingerprint = ''

    mapState.value = 'connecting'
    void restoreMapSnapshot(nextSnapshotScope, currentGeneration)
    const api = createAuthenticatedClient()
    let next: MapHubClient
    next = api.createMapHub({
      onBatterySoc: (value) => {
        if (currentGeneration !== generation || next !== hub) return
        updateBatteryPercent(value)
        markTelemetryReady()
      },
      onBatteryVoltage: (value) => {
        if (currentGeneration !== generation || next !== hub) return
        updateBatteryVoltage(value)
        markTelemetryReady()
      },
      onSafeTwist: (linear, angular) => {
        if (currentGeneration !== generation || next !== hub || !queueSafeTwist(linear, angular)) return
        markTelemetryReady()
      },
      onOdometry: (value) => {
        if (currentGeneration !== generation || next !== hub || !queueOdometry(value)) return
        markTelemetryReady()
      },
      onCameraImage: (value) => {
        if (currentGeneration !== generation || next !== hub) return
        cameraImage.value = value
        markTelemetryReady()
      },
      onMap: (value) => {
        if (currentGeneration !== generation || next !== hub) return
        clearMapFrameRetry()
        const receivedAt = Date.now()
        mapFrame.value = value
        mapFrameSource.value = 'live'
        mapFrameCachedAt.value = receivedAt
        mapFrameIssue.value = ''
        markFrame('map')
        queueMapSnapshotSave(nextSnapshotScope, value, receivedAt)
        markTelemetryReady()
      },
      onFrameError: (event, error) => {
        if (currentGeneration !== generation || next !== hub) return
        if (event === 'Map') {
          mapFrameIssue.value = describeMapFrameIssue(error)
          return
        }
        telemetryError.value = `${event} 帧解码失败：${error instanceof Error ? error.message : '未知错误'}`
      },
      onRobotPose: (value) => {
        if (currentGeneration !== generation || next !== hub) return
        robotPose.value = value
        markFrame('robotPose')
        markTelemetryReady()
      },
      onParticles: (value) => {
        if (currentGeneration !== generation || next !== hub) return
        particles.value = value
        markFrame('particles')
      },
      onPath: (value) => {
        if (currentGeneration !== generation || next !== hub) return
        globalPath.value = value
        markFrame('path')
      },
      onLocalPlan: (value) => {
        if (currentGeneration !== generation || next !== hub) return
        localPlan.value = value
        markFrame('localPlan')
      },
      onScan: (value) => {
        if (currentGeneration !== generation || next !== hub) return
        scan.value = value
        markFrame('scan')
      },
      onGlobalCostmap: (value) => {
        if (currentGeneration !== generation || next !== hub) return
        globalCostmap.value = value
        markFrame('globalCostmap')
      },
      onLocalCostmap: (value) => {
        if (currentGeneration !== generation || next !== hub) return
        localCostmap.value = value
        markFrame('localCostmap')
      },
      onMapPatch: (value) => {
        if (currentGeneration !== generation || next !== hub) return
        mapPatch.value = value
        markFrame('mapPatch')
      },
    })

    next.connection.onreconnecting(() => {
      if (currentGeneration !== generation || next !== hub) return
      mapState.value = 'connecting'
      resetTelemetryValues({ preserveMap: true })
    })
    next.connection.onreconnected(() => {
      if (currentGeneration !== generation || next !== hub) return
      mapState.value = 'ok'
      telemetryError.value = ''
      scheduleMapFrameRetry(next, currentGeneration)
    })
    next.connection.onclose((error?: Error) => {
      if (currentGeneration !== generation || next !== hub) return
      hub = null
      mapState.value = 'off'
      resetTelemetryValues({ preserveMap: true })
      telemetryError.value = error?.message || '遥测连接已断开。'
    })

    hub = next
    try {
      await next.start()
      if (currentGeneration !== generation || next !== hub) {
        await next.stop().catch(() => {})
        return
      }
      await next.subscribe()
      if (currentGeneration !== generation || next !== hub) {
        await next.stop().catch(() => {})
        return
      }
      mapState.value = 'ok'
      scheduleMapFrameRetry(next, currentGeneration)
    } catch (error) {
      if (currentGeneration !== generation || next !== hub) return
      hub = null
      mapState.value = 'off'
      telemetryError.value = describeTelemetryError(error)
      await next.stop().catch(() => {})
    }
  }

  function getNavigationHub(pose: NavigationPose) {
    if (![pose.x, pose.y, pose.theta].every(Number.isFinite)) {
      throw new Error('导航姿态包含无效坐标。')
    }
    if (!hub || mapState.value !== 'ok') {
      throw new Error('MapHub 未连接，无法提交导航姿态。')
    }
    if (mapFrameSource.value !== 'live') {
      throw new Error('当前仅显示缓存地图，等待新的 Map 地图帧。')
    }
    return hub
  }

  async function setGoal(pose: NavigationPose) {
    const current = getNavigationHub(pose)
    await current.setGoal(pose.x, pose.y, pose.theta)
  }

  async function setInitialPose(pose: NavigationPose) {
    const current = getNavigationHub(pose)
    await current.setInitialPose(pose.x, pose.y, pose.theta)
  }

  onMounted(() => {
    stopSessionWatch = watch(
      () => session.value?.token,
      () => void connectForSession(session.value),
      { immediate: true },
    )
  })

  onUnmounted(() => {
    generation += 1
    stopSessionWatch?.()
    if (!session.value) {
      const scope = mapSnapshotScope
      mapSnapshotScope = ''
      mapSnapshotFingerprint = ''
      queueMapSnapshotClear(scope)
    }
    resetTelemetryValues()
    void stopHub()
  })

  return {
    mapState,
    batteryPercent,
    batteryVoltage,
    cameraImage,
    mapFrame,
    mapFrameSource,
    mapFrameCachedAt,
    robotPose,
    particles,
    globalPath,
    localPlan,
    scan,
    globalCostmap,
    localCostmap,
    mapPatch,
    odometry,
    safeTwist,
    telemetryError,
    mapFrameIssue,
    telemetryMetrics,
    setGoal,
    setInitialPose,
  }
}
