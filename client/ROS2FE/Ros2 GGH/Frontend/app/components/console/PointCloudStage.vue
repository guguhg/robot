<script lang="ts">
const WEBGL_CREATION_COOLDOWN_MS = 15_000
let webglCreationBlockedUntil = 0

function isWebglCreationBlocked() {
  return Date.now() < webglCreationBlockedUntil
}

function blockWebglCreation() {
  webglCreationBlockedUntil = Date.now() + WEBGL_CREATION_COOLDOWN_MS
}
</script>

<script setup lang="ts">
import * as THREE from 'three'
import { OrbitControls } from 'three/examples/jsm/controls/OrbitControls.js'
import {
  CWebApiError,
  type PointCloudFrame,
  type PointCloudHubClient,
  type PointCloudStreamInfo,
} from '@/lib/cwebapi/cwebapi-client'
import type { AuthSession } from '@/composables/useAuth'
import type { LinkState } from '@/composables/useRobot'

type ColorMode = 'solid' | 'height' | 'distance'
type ExportFormat = 'ply' | 'csv'
type PointCloudViewPreset = 'auto' | 'scan-top' | 'points-3d' | 'custom'
type ActiveViewPreset = Exclude<PointCloudViewPreset, 'auto'>
type RenderMode = 'active' | 'idle'

const POINT_SIZE_MIN = 0.01
const POINT_SIZE_MAX = 0.12
const POINT_SIZE_STEP = 0.005
const DEFAULT_VISIBLE_POINTS = 50_000
const MAX_VISIBLE_POINTS = 200_000
const MAX_RENDER_PIXEL_RATIO = 1.5
const FIRST_FRAME_WAIT_TIMEOUT_MS = 3_000
const SOLID_POINT_COLOR: [number, number, number] = [37 / 255, 99 / 255, 235 / 255]
const WEBGL_CREATION_ERROR = '浏览器暂时无法创建点云画布，已暂停感知连接以避免页面卡顿。请关闭其他图形密集网页后刷新页面。'
const WEBGL_CONTEXT_LOST_ERROR = '浏览器图形渲染已停止，点云连接已暂停以避免页面卡顿。请关闭其他图形密集网页后刷新页面。'

interface PointBounds {
  minX: number
  minY: number
  minZ: number
  maxX: number
  maxY: number
  maxZ: number
}

interface PointFilterConfig {
  rangeEnabled: boolean
  minRangeSquared: number
  maxRangeSquared: number
  heightEnabled: boolean
  minHeight: number
  maxHeight: number
}

interface PointFrameAnalysis {
  finite: number
  filtered: number
  bounds: PointBounds | null
  minDistance: number
  maxDistance: number
}

interface PointCloudSessionSnapshot {
  scope: string
  streamName: string
  topic: string
  capturedAt: number
  displayedPointCount: number
  sourcePointCount: number
  validPointCount: number
  filteredPointCount: number
  bounds: PointBounds
  positions: Float32Array
  colors: Float32Array
}

function getPointCloudSnapshotScope(apiBase: unknown, currentSession: AuthSession) {
  const endpoint = String(apiBase ?? '').trim().replace(/\/+$/, '').toLowerCase()
  const account = currentSession.isGuest ? 'guest' : 'account'
  return [
    endpoint || 'default-endpoint',
    account,
    currentSession.username.trim().toLowerCase(),
    currentSession.email.trim().toLowerCase(),
  ].join('|')
}

function copyBounds(bounds: PointBounds): PointBounds {
  return { ...bounds }
}

function hasFiniteBounds(bounds: PointBounds) {
  return [bounds.minX, bounds.minY, bounds.minZ, bounds.maxX, bounds.maxY, bounds.maxZ].every(Number.isFinite)
}

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
  renderMode: RenderMode
  lastFrameAt: number
  paused: boolean
  frameHeld: boolean
  cachedSnapshot: boolean
  awaitingFirstFrame: boolean
  firstFrameTimedOut: boolean
  error: string
  maxVisiblePoints: number
  colorMode: ColorMode
  viewPreset: ActiveViewPreset
}

const emit = defineEmits<{
  (event: 'state', value: LinkState): void
  (event: 'diagnostics', value: PointCloudDiagnostics): void
}>()
const { session, createAuthenticatedClient } = useAuth()
const runtimeConfig = useRuntimeConfig()

const canvasEl = ref<HTMLCanvasElement | null>(null)
const hostEl = ref<HTMLElement | null>(null)
const streams = ref<PointCloudStreamInfo[]>([])
const selectedStreamName = ref('')
const preferredStreamName = useState<string>('console:pointcloud:stream', () => '')
const state = ref<LinkState>('off')
const errorMessage = ref('')
const pointCount = ref(0)
const sourcePointCount = ref(0)
const validPointCount = ref(0)
const filteredPointCount = ref(0)
const loading = ref(false)
const paused = ref(false)
const pointSize = useState<number>('console:pointcloud:point-size', () => 0.045)
const maxVisiblePoints = useState<number>('console:pointcloud:max-visible-points', () => DEFAULT_VISIBLE_POINTS)
const colorMode = useState<ColorMode>('console:pointcloud:color-mode', () => 'solid')
const viewPreset = useState<PointCloudViewPreset>('console:pointcloud:view-preset', () => 'auto')
const showGrid = useState<boolean>('console:pointcloud:show-grid', () => false)
const showAxes = useState<boolean>('console:pointcloud:show-axes', () => false)
const rangeFilterEnabled = useState<boolean>('console:pointcloud:range-filter-enabled', () => false)
const rangeMinMeters = useState<number>('console:pointcloud:range-min-meters', () => 0)
const rangeMaxMeters = useState<number>('console:pointcloud:range-max-meters', () => 20)
const heightFilterEnabled = useState<boolean>('console:pointcloud:height-filter-enabled', () => false)
const heightMinMeters = useState<number>('console:pointcloud:height-min-meters', () => -1.5)
const heightMaxMeters = useState<number>('console:pointcloud:height-max-meters', () => 2)
const exportFormat = useState<ExportFormat>('console:pointcloud:export-format', () => 'ply')
const dataFps = ref(0)
const renderFps = ref(0)
const renderMode = ref<RenderMode>('idle')
const lastFrameAt = ref(0)
const frameHeld = ref(false)
const cachedSnapshot = ref(false)
const firstFrameTimedOut = ref(false)
const webglUnavailable = ref(false)
const pointCloudSessionSnapshot = useState<PointCloudSessionSnapshot | null>('console:pointcloud:session-snapshot', () => null)

let renderer: THREE.WebGLRenderer | null = null
let scene: THREE.Scene | null = null
let camera: THREE.PerspectiveCamera | null = null
let controls: OrbitControls | null = null
let geometry: THREE.BufferGeometry | null = null
let material: THREE.PointsMaterial | null = null
let positionAttribute: THREE.BufferAttribute | null = null
let colorAttribute: THREE.BufferAttribute | null = null
let rendererCanvas: HTMLCanvasElement | null = null
let positionBuffer = new Float32Array(0)
let colorBuffer = new Float32Array(0)
let pointCapacity = 0
let gridHelper: THREE.GridHelper | null = null
let axesHelper: THREE.AxesHelper | null = null
let sensorGuide: THREE.Group | null = null
let animationFrame = 0
let pointBufferUpdateFrame = 0
let stageActive = true
let pendingPointBufferFrame: PointCloudFrame | null = null
let pendingPointBufferAllowsPaused = false
let resizeObserver: ResizeObserver | null = null
let hub: PointCloudHubClient | null = null
let removeFrameHandler: (() => boolean) | null = null
let subscribedStream = ''
let connectionVersion = 0
let stopSessionWatch: (() => void) | undefined
let needsCameraFit = true
let latestFrame: PointCloudFrame | null = null
let visibleFrame: PointCloudFrame | null = null
let visibleBounds: PointBounds | null = null
let dataWindowStarted = 0
let dataWindowFrames = 0
let renderWindowStarted = 0
let renderWindowFrames = 0
let diagnosticsTimer: ReturnType<typeof setTimeout> | undefined
let frameStaleTimer: ReturnType<typeof setTimeout> | undefined
let firstFrameTimer: ReturnType<typeof setTimeout> | undefined
let renderIdleTimer: ReturnType<typeof setTimeout> | undefined
let lastDiagnosticsAt = 0
let pointCloudSnapshotScope = ''

pointSize.value = Number.isFinite(pointSize.value)
  ? Math.max(POINT_SIZE_MIN, Math.min(POINT_SIZE_MAX, pointSize.value))
  : 0.045
maxVisiblePoints.value = Number.isFinite(maxVisiblePoints.value)
  ? Math.max(1, Math.min(MAX_VISIBLE_POINTS, Math.round(maxVisiblePoints.value)))
  : DEFAULT_VISIBLE_POINTS
if (!['solid', 'height', 'distance'].includes(colorMode.value)) colorMode.value = 'solid'
if (!['auto', 'scan-top', 'points-3d', 'custom'].includes(viewPreset.value)) viewPreset.value = 'auto'
if (typeof showGrid.value !== 'boolean') showGrid.value = false
if (typeof showAxes.value !== 'boolean') showAxes.value = false
if (typeof rangeFilterEnabled.value !== 'boolean') rangeFilterEnabled.value = false
if (typeof heightFilterEnabled.value !== 'boolean') heightFilterEnabled.value = false
if (exportFormat.value !== 'ply' && exportFormat.value !== 'csv') exportFormat.value = 'ply'
if (typeof preferredStreamName.value !== 'string') preferredStreamName.value = ''

const selectedStream = computed(() => streams.value.find(stream => stream.name === selectedStreamName.value) ?? null)
const stateLabel = computed(() => state.value === 'ok' ? 'LIVE' : state.value === 'connecting' ? 'LINKING' : 'OFF')
const hasReceivedFrame = computed(() => lastFrameAt.value > 0)
const isFrameHeld = computed(() => frameHeld.value && hasReceivedFrame.value)
const isWaitingForFirstFrame = computed(() => state.value === 'connecting' && !hasReceivedFrame.value && !cachedSnapshot.value)
const isFirstFrameDelayed = computed(() => isWaitingForFirstFrame.value && firstFrameTimedOut.value)
const cloudStateTone = computed(() => cachedSnapshot.value ? 'cached' : isFrameHeld.value ? 'held' : state.value)
const cloudStateLabel = computed(() => {
  if (cachedSnapshot.value) return 'CACHED'
  if (isFrameHeld.value) return 'FRAME HOLD'
  if (isFirstFrameDelayed.value) return 'NO FRAME'
  if (paused.value && state.value === 'ok') return 'FROZEN'
  return stateLabel.value
})
const streamPointLimit = computed(() => pointLimitFor(selectedStream.value))
const pointLimitMin = computed(() => Math.min(1000, streamPointLimit.value))
const pointLimitStep = computed(() => streamPointLimit.value < 1000 ? 1 : 1000)
const activeViewPreset = computed<ActiveViewPreset>(() => resolveViewPreset(viewPreset.value, selectedStream.value))
const viewPresetLabel = computed(() => {
  if (activeViewPreset.value === 'scan-top') return 'PERIMETER'
  if (activeViewPreset.value === 'points-3d') return 'SPATIAL'
  return 'CUSTOM'
})
const spatialSceneLabel = computed(() => (
  activeViewPreset.value === 'scan-top' ? 'PERIMETER SENSE' : 'SPATIAL SCAN'
))
const spatialSceneMeta = computed(() => {
  if (!selectedStream.value) return 'SENSOR · WAIT'
  if (isFirstFrameDelayed.value) return 'HUB · NO FRAME'
  if (isWaitingForFirstFrame.value) return 'SENSOR · LINKING'
  if (cachedSnapshot.value) {
    if (sourcePointCount.value === 0) return 'NO RETURNS · CACHED'
    if (pointCount.value === 0) return 'FILTERED VIEW · CACHED'
    return activeViewPreset.value === 'scan-top'
      ? `${pointCount.value.toLocaleString()} CONTACTS · CACHED`
      : `${pointCount.value.toLocaleString()} POINTS · CACHED`
  }
  if (isFrameHeld.value) {
    if (sourcePointCount.value === 0) return 'NO RETURNS · FRAME HOLD'
    if (pointCount.value === 0) return 'FILTERED VIEW · FRAME HOLD'
    return activeViewPreset.value === 'scan-top'
      ? `${pointCount.value.toLocaleString()} CONTACTS · FRAME HOLD`
      : `${pointCount.value.toLocaleString()} POINTS · FRAME HOLD`
  }
  if (hasReceivedFrame.value && sourcePointCount.value === 0) return 'CURRENT FRAME · NO RETURNS'
  if (sourcePointCount.value > 0 && pointCount.value === 0) return 'FILTERED VIEW · 0 CONTACTS'
  if (paused.value) {
    return activeViewPreset.value === 'scan-top'
      ? `${pointCount.value.toLocaleString()} CONTACTS · FROZEN`
      : `${pointCount.value.toLocaleString()} POINTS · FROZEN`
  }
  return activeViewPreset.value === 'scan-top'
    ? `${pointCount.value.toLocaleString()} CONTACTS`
    : `${pointCount.value.toLocaleString()} POINTS`
})
const validPointRatio = computed(() => (
  sourcePointCount.value > 0 ? (validPointCount.value / sourcePointCount.value) * 100 : 0
))
const filteredOutPointCount = computed(() => Math.max(0, validPointCount.value - filteredPointCount.value))

function finiteNumber(value: number, fallback: number) {
  return Number.isFinite(value) ? value : fallback
}

function normalizeFilterBounds() {
  const minimumRange = Math.max(0, finiteNumber(rangeMinMeters.value, 0))
  const maximumRange = Math.max(minimumRange, finiteNumber(rangeMaxMeters.value, 20))
  const minimumHeight = finiteNumber(heightMinMeters.value, -1.5)
  const maximumHeight = Math.max(minimumHeight, finiteNumber(heightMaxMeters.value, 2))
  rangeMinMeters.value = minimumRange
  rangeMaxMeters.value = maximumRange
  heightMinMeters.value = minimumHeight
  heightMaxMeters.value = maximumHeight
}

normalizeFilterBounds()

function pointLimitFor(stream: PointCloudStreamInfo | null) {
  return Math.max(1, Math.min(MAX_VISIBLE_POINTS, stream?.maxPoints || DEFAULT_VISIBLE_POINTS))
}

function clampVisiblePointLimit(stream: PointCloudStreamInfo) {
  const maximum = pointLimitFor(stream)
  const minimum = Math.min(1000, maximum)
  const requested = Number.isFinite(maxVisiblePoints.value) ? Math.round(maxVisiblePoints.value) : DEFAULT_VISIBLE_POINTS
  maxVisiblePoints.value = Math.max(minimum, Math.min(maximum, requested))
}

function selectInitialStream(list: PointCloudStreamInfo[]) {
  return list.find(stream => stream.name === preferredStreamName.value)
    ?? list.find(stream => stream.topic === '/scan' || stream.name === 'scan')
    ?? list.find(stream => stream.name === 'points2')
    ?? list[0]
    ?? null
}

function isScanStream(stream: PointCloudStreamInfo | null) {
  if (!stream) return false
  return stream.topic === '/scan'
    || stream.name.toLowerCase() === 'scan'
    || stream.messageType.toLowerCase().includes('laserscan')
}

function resolveViewPreset(
  preset: PointCloudViewPreset,
  stream: PointCloudStreamInfo | null,
): ActiveViewPreset {
  if (preset === 'scan-top' || preset === 'points-3d' || preset === 'custom') return preset
  return isScanStream(stream) ? 'scan-top' : 'points-3d'
}

watch(state, (value) => {
  emit('state', value)
  publishDiagnostics(true)
}, { immediate: true })

watch(pointSize, (value) => {
  if (material) material.size = value
  scheduleRender()
  publishDiagnostics()
})

watch(colorMode, () => {
  applyColorMode()
  if (colorMode.value === 'solid') scheduleRender()
  else refreshVisiblePointBuffer()
  publishDiagnostics()
})

watch(maxVisiblePoints, (value) => {
  const shouldRestoreSnapshot = cachedSnapshot.value && !latestFrame
  resetPointBuffer(value)
  if (shouldRestoreSnapshot) restorePointCloudSnapshot(selectedStream.value)
  else refreshVisiblePointBuffer()
  publishDiagnostics()
})

watch(showGrid, () => {
  updateSensorGuide()
  scheduleRender()
  publishDiagnostics()
})

watch(showAxes, () => {
  updateSensorGuide()
  scheduleRender()
  publishDiagnostics()
})

watch(activeViewPreset, () => {
  updateSensorGuide()
  scheduleRender()
  publishDiagnostics()
})

watch(
  [rangeFilterEnabled, rangeMinMeters, rangeMaxMeters, heightFilterEnabled, heightMinMeters, heightMaxMeters],
  () => {
    normalizeFilterBounds()
    refreshVisiblePointBuffer()
    publishDiagnostics()
  },
)

function suspendWebgl(message: string, blockCreation = true) {
  if (webglUnavailable.value) return
  webglUnavailable.value = true
  if (blockCreation) blockWebglCreation()
  connectionVersion += 1
  loading.value = false
  disposeScene()
  state.value = 'off'
  errorMessage.value = message
  void stopHub()
  publishDiagnostics(true)
}

function onWebglContextLost() {
  suspendWebgl(WEBGL_CONTEXT_LOST_ERROR)
}

function onWebglContextCreationError() {
  suspendWebgl(WEBGL_CREATION_ERROR)
}

function scheduleRender() {
  if (!stageActive || webglUnavailable.value || !renderer || !scene || !camera || animationFrame) return
  renderMode.value = 'active'
  animationFrame = requestAnimationFrame(renderScene)
}

function renderScene(now: number) {
  animationFrame = 0
  if (!stageActive || webglUnavailable.value || !renderer || !scene || !camera) return

  const controlsChanged = controls?.update() ?? false
  try {
    renderer.render(scene, camera)
  } catch {
    suspendWebgl(WEBGL_CONTEXT_LOST_ERROR)
    return
  }
  renderWindowFrames += 1
  if (!renderWindowStarted) renderWindowStarted = now
  const elapsed = now - renderWindowStarted
  if (elapsed >= 500) {
    renderFps.value = renderWindowFrames * 1000 / elapsed
    renderWindowFrames = 0
    renderWindowStarted = now
    publishDiagnostics()
  }

  clearTimeout(renderIdleTimer)
  renderIdleTimer = setTimeout(() => {
    if (animationFrame) return
    renderFps.value = 0
    publishDiagnostics()
  }, 650)

  if (controlsChanged) scheduleRender()
  else renderMode.value = 'idle'
}

function handleControlsStart() {
  if (viewPreset.value !== 'custom') {
    viewPreset.value = 'custom'
    if (controls) {
      controls.minPolarAngle = 0.001
      controls.maxPolarAngle = Math.PI - 0.001
    }
  }
  scheduleRender()
  publishDiagnostics()
}

function createSensorGuide() {
  const group = new THREE.Group()
  group.name = 'perimeter-guide'
  const createRing = (radius: number, opacity: number) => {
    const points: THREE.Vector3[] = []
    for (let index = 0; index < 64; index += 1) {
      const angle = (index / 64) * Math.PI * 2
      points.push(new THREE.Vector3(Math.cos(angle) * radius, 0.002, Math.sin(angle) * radius))
    }
    const geometry = new THREE.BufferGeometry().setFromPoints(points)
    const material = new THREE.LineBasicMaterial({
      color: 0x0f766e,
      transparent: true,
      opacity,
      depthWrite: false,
    })
    group.add(new THREE.LineLoop(geometry, material))
  }
  createRing(1 / 3, 0.09)
  createRing(2 / 3, 0.14)
  createRing(1, 0.22)

  const tickPositions: number[] = []
  for (let index = 0; index < 12; index += 1) {
    const angle = (index / 12) * Math.PI * 2
    const inner = 0.9
    tickPositions.push(
      Math.cos(angle) * inner, 0.003, Math.sin(angle) * inner,
      Math.cos(angle), 0.003, Math.sin(angle),
    )
  }
  const ticks = new THREE.BufferGeometry()
  ticks.setAttribute('position', new THREE.Float32BufferAttribute(tickPositions, 3))
  group.add(new THREE.LineSegments(ticks, new THREE.LineBasicMaterial({
    color: 0x0f766e,
    transparent: true,
    opacity: 0.25,
    depthWrite: false,
  })))

  const origin = new THREE.Mesh(
    new THREE.RingGeometry(0.026, 0.042, 24),
    new THREE.MeshBasicMaterial({
      color: 0x0f766e,
      transparent: true,
      opacity: 0.42,
      depthWrite: false,
      side: THREE.DoubleSide,
    }),
  )
  origin.rotation.x = -Math.PI / 2
  origin.position.y = 0.004
  group.add(origin)
  return group
}

function updateSensorGuide() {
  const isPerimeter = activeViewPreset.value === 'scan-top'
  if (gridHelper) gridHelper.visible = showGrid.value && !isPerimeter
  if (axesHelper) axesHelper.visible = showAxes.value && !isPerimeter
  if (!sensorGuide) return

  sensorGuide.visible = isPerimeter
  if (!isPerimeter) return
  const bounds = visibleBounds
  const extent = bounds
    ? Math.max(
        Math.abs(bounds.minX),
        Math.abs(bounds.maxX),
        Math.abs(bounds.minZ),
        Math.abs(bounds.maxZ),
        1,
      )
    : 3
  const scale = Math.max(1, Math.min(12, Math.ceil(extent)))
  sensorGuide.scale.setScalar(scale)
}

function disposeSensorGuide() {
  if (!sensorGuide) return
  scene?.remove(sensorGuide)
  sensorGuide.traverse(child => {
    if (!(child instanceof THREE.Line) && !(child instanceof THREE.Mesh)) return
    child.geometry.dispose()
    const materials = Array.isArray(child.material) ? child.material : [child.material]
    materials.forEach(material => material.dispose())
  })
  sensorGuide = null
}

function describeError(error: unknown) {
  if (!(error instanceof CWebApiError)) return error instanceof Error ? error.message : '点云连接失败'
  if (error.status === 401) return '登录状态已失效。'
  if (error.status === 503) return '点云服务暂不可用。'
  if (error.status === 0) return '无法连接点云服务。'
  return error.message || '点云连接失败'
}

function initScene() {
  if (!canvasEl.value || !hostEl.value || renderer) return Boolean(renderer)
  if (isWebglCreationBlocked()) {
    suspendWebgl(WEBGL_CREATION_ERROR, false)
    return false
  }

  webglUnavailable.value = false
  rendererCanvas = canvasEl.value
  rendererCanvas.addEventListener('webglcontextlost', onWebglContextLost, { once: true })
  rendererCanvas.addEventListener('webglcontextcreationerror', onWebglContextCreationError, { once: true })

  try {
    renderer = new THREE.WebGLRenderer({
      canvas: rendererCanvas,
      antialias: true,
      alpha: true,
      powerPreference: 'high-performance',
    })
  } catch {
    suspendWebgl(WEBGL_CREATION_ERROR)
    return false
  }
  renderer.setPixelRatio(Math.min(window.devicePixelRatio || 1, MAX_RENDER_PIXEL_RATIO))
  renderer.setClearColor(0x000000, 0)

  scene = new THREE.Scene()
  camera = new THREE.PerspectiveCamera(52, 1, 0.01, 2000)
  camera.position.set(5, 3.5, 5)

  controls = new OrbitControls(camera, renderer.domElement)
  controls.enableDamping = true
  controls.dampingFactor = 0.08
  controls.target.set(0, 0.5, 0)
  controls.update()
  controls.addEventListener('change', scheduleRender)
  controls.addEventListener('start', handleControlsStart)

  gridHelper = new THREE.GridHelper(20, 40, 0x9aa3b2, 0xd7dbe2)
  const gridMaterial = gridHelper.material as THREE.Material
  gridMaterial.transparent = true
  gridMaterial.opacity = 0.55
  scene.add(gridHelper)
  axesHelper = new THREE.AxesHelper(0.8)
  scene.add(axesHelper)
  sensorGuide = createSensorGuide()
  scene.add(sensorGuide)
  updateSensorGuide()

  geometry = new THREE.BufferGeometry()
  material = new THREE.PointsMaterial({
    size: pointSize.value,
    sizeAttenuation: true,
    transparent: true,
    opacity: 0.9,
    color: new THREE.Color(...SOLID_POINT_COLOR),
    vertexColors: colorMode.value !== 'solid',
  })
  scene.add(new THREE.Points(geometry, material))
  resetPointBuffer(maxVisiblePoints.value)

  const resize = () => {
    if (!renderer || !camera || !hostEl.value) return
    const width = Math.max(1, hostEl.value.clientWidth)
    const height = Math.max(1, hostEl.value.clientHeight)
    try {
      renderer.setSize(width, height, false)
      camera.aspect = width / height
      camera.updateProjectionMatrix()
      scheduleRender()
    } catch {
      suspendWebgl(WEBGL_CONTEXT_LOST_ERROR)
    }
  }
  resizeObserver = new ResizeObserver(resize)
  resizeObserver.observe(hostEl.value)
  resize()
  if (webglUnavailable.value) return false

  renderWindowStarted = performance.now()
  scheduleRender()
  return true
}

function resetPointBuffer(maxPoints: number) {
  if (!geometry) return
  pointCapacity = Math.max(1, Math.min(200_000, Math.floor(maxPoints || 50_000)))
  positionBuffer = new Float32Array(pointCapacity * 3)
  colorBuffer = new Float32Array(pointCapacity * 3)
  positionAttribute = new THREE.BufferAttribute(positionBuffer, 3)
  colorAttribute = new THREE.BufferAttribute(colorBuffer, 3)
  positionAttribute.setUsage(THREE.DynamicDrawUsage)
  colorAttribute.setUsage(THREE.DynamicDrawUsage)
  geometry.setAttribute('position', positionAttribute)
  geometry.setAttribute('color', colorAttribute)
  geometry.setDrawRange(0, 0)
  pointCount.value = 0
  sourcePointCount.value = 0
  validPointCount.value = 0
  filteredPointCount.value = 0
  visibleBounds = null
  needsCameraFit = true
  updateSensorGuide()
  scheduleRender()
}

function clearPointCloudSessionSnapshot() {
  pointCloudSessionSnapshot.value = null
  cachedSnapshot.value = false
}

function rememberVisiblePointCloud() {
  const stream = selectedStream.value
  const visible = Math.min(
    pointCount.value,
    pointCapacity,
    Math.floor(positionBuffer.length / 3),
    Math.floor(colorBuffer.length / 3),
  )
  if (!pointCloudSnapshotScope || !stream || !visibleBounds || visible <= 0) return

  const colors = colorBuffer.slice(0, visible * 3)
  if (colorMode.value === 'solid') {
    for (let index = 0; index < colors.length; index += 3) {
      colors[index] = SOLID_POINT_COLOR[0]
      colors[index + 1] = SOLID_POINT_COLOR[1]
      colors[index + 2] = SOLID_POINT_COLOR[2]
    }
  }

  pointCloudSessionSnapshot.value = {
    scope: pointCloudSnapshotScope,
    streamName: stream.name,
    topic: stream.topic,
    capturedAt: Date.now(),
    displayedPointCount: visible,
    sourcePointCount: sourcePointCount.value,
    validPointCount: validPointCount.value,
    filteredPointCount: filteredPointCount.value,
    bounds: copyBounds(visibleBounds),
    positions: positionBuffer.slice(0, visible * 3),
    colors,
  }
}

function restorePointCloudSnapshot(stream: PointCloudStreamInfo | null) {
  const snapshot = pointCloudSessionSnapshot.value
  if (
    !snapshot
    || !stream
    || snapshot.scope !== pointCloudSnapshotScope
    || snapshot.streamName !== stream.name
    || snapshot.topic !== stream.topic
    || !Number.isFinite(snapshot.capturedAt)
    || snapshot.capturedAt <= 0
    || !Number.isSafeInteger(snapshot.displayedPointCount)
    || snapshot.displayedPointCount <= 0
    || !(snapshot.positions instanceof Float32Array)
    || !(snapshot.colors instanceof Float32Array)
    || !hasFiniteBounds(snapshot.bounds)
  ) {
    if (snapshot && snapshot.scope === pointCloudSnapshotScope && stream && snapshot.streamName === stream.name) {
      pointCloudSessionSnapshot.value = null
    }
    cachedSnapshot.value = false
    return false
  }

  const visible = Math.min(
    snapshot.displayedPointCount,
    pointCapacity,
    Math.floor(snapshot.positions.length / 3),
    Math.floor(snapshot.colors.length / 3),
  )
  if (visible <= 0 || !geometry || !positionAttribute || !colorAttribute) {
    cachedSnapshot.value = false
    return false
  }

  positionBuffer.set(snapshot.positions.subarray(0, visible * 3))
  colorBuffer.set(snapshot.colors.subarray(0, visible * 3))
  geometry.setDrawRange(0, visible)
  positionAttribute.clearUpdateRanges()
  positionAttribute.addUpdateRange(0, visible * 3)
  positionAttribute.needsUpdate = true
  colorAttribute.clearUpdateRanges()
  colorAttribute.addUpdateRange(0, visible * 3)
  colorAttribute.needsUpdate = true
  pointCount.value = visible
  sourcePointCount.value = Math.max(visible, snapshot.sourcePointCount)
  validPointCount.value = Math.max(visible, snapshot.validPointCount)
  filteredPointCount.value = Math.max(visible, snapshot.filteredPointCount)
  visibleBounds = copyBounds(snapshot.bounds)
  lastFrameAt.value = snapshot.capturedAt
  dataFps.value = 0
  frameHeld.value = false
  cachedSnapshot.value = true
  needsCameraFit = true
  updateSensorGuide()
  fitCamera(visibleBounds)
  needsCameraFit = false
  scheduleRender()
  return true
}

function fitCamera(bounds: PointBounds) {
  if (!camera || !controls) return
  const center = new THREE.Vector3(
    (bounds.minX + bounds.maxX) / 2,
    (bounds.minY + bounds.maxY) / 2,
    (bounds.minZ + bounds.maxZ) / 2,
  )
  const topDown = activeViewPreset.value === 'scan-top'
  const extent = topDown
    ? Math.max(bounds.maxX - bounds.minX, bounds.maxZ - bounds.minZ, 1)
    : Math.max(
        bounds.maxX - bounds.minX,
        bounds.maxY - bounds.minY,
        bounds.maxZ - bounds.minZ,
        1,
      )
  const distance = extent * (topDown ? 1.2 : 1.45)
  controls.target.copy(center)
  if (topDown) {
    camera.up.set(0, 0, -1)
    controls.minPolarAngle = 0.001
    controls.maxPolarAngle = 0.001
    camera.position.set(center.x, center.y + distance, center.z + distance * 0.001)
  } else {
    camera.up.set(0, 1, 0)
    controls.minPolarAngle = 0.001
    controls.maxPolarAngle = Math.PI - 0.001
    camera.position.set(center.x + distance, center.y + distance * 0.7, center.z + distance)
  }
  camera.near = Math.max(0.01, distance / 1000)
  camera.far = Math.max(200, distance * 30)
  camera.updateProjectionMatrix()
  controls.update()
  scheduleRender()
}

function applyViewPreset() {
  needsCameraFit = true
  if (visibleBounds) {
    fitCamera(visibleBounds)
    needsCameraFit = false
  }
  updateSensorGuide()
  scheduleRender()
  publishDiagnostics()
}

function createPointFilter(): PointFilterConfig {
  const rangeEnabled = rangeFilterEnabled.value
  const minRange = rangeEnabled ? rangeMinMeters.value : 0
  const maxRange = rangeEnabled ? rangeMaxMeters.value : 0
  return {
    rangeEnabled,
    minRangeSquared: minRange * minRange,
    maxRangeSquared: maxRange * maxRange,
    heightEnabled: heightFilterEnabled.value,
    minHeight: heightMinMeters.value,
    maxHeight: heightMaxMeters.value,
  }
}

function passesFilters(
  x: number,
  y: number,
  z: number,
  filter: PointFilterConfig,
  distanceSquared: number,
) {
  if (filter.heightEnabled && (z < filter.minHeight || z > filter.maxHeight)) return false
  return !filter.rangeEnabled
    || (distanceSquared >= filter.minRangeSquared && distanceSquared <= filter.maxRangeSquared)
}

function analyzePointFrame(
  frame: PointCloudFrame,
  available: number,
  filter: PointFilterConfig,
  trackDistance: boolean,
): PointFrameAnalysis {
  let finite = 0
  let filtered = 0
  let minX = Infinity
  let minY = Infinity
  let minZ = Infinity
  let maxX = -Infinity
  let maxY = -Infinity
  let maxZ = -Infinity
  let minDistance = Infinity
  let maxDistance = -Infinity
  const needsDistanceSquared = filter.rangeEnabled || trackDistance

  for (let index = 0; index < available; index += 1) {
    const source = index * 3
    const rosX = frame.xyz[source]!
    const rosY = frame.xyz[source + 1]!
    const rosZ = frame.xyz[source + 2]!
    if (!Number.isFinite(rosX) || !Number.isFinite(rosY) || !Number.isFinite(rosZ)) continue
    finite += 1

    const distanceSquared = needsDistanceSquared ? rosX * rosX + rosY * rosY + rosZ * rosZ : 0
    if (!passesFilters(rosX, rosY, rosZ, filter, distanceSquared)) continue
    filtered += 1

    /* ROS: x 前 / y 左 / z 上 → Three.js: x / y 上 / z 后。 */
    const x = rosX
    const y = rosZ
    const z = -rosY
    minX = Math.min(minX, x)
    minY = Math.min(minY, y)
    minZ = Math.min(minZ, z)
    maxX = Math.max(maxX, x)
    maxY = Math.max(maxY, y)
    maxZ = Math.max(maxZ, z)
    if (trackDistance) {
      const distance = Math.sqrt(distanceSquared)
      minDistance = Math.min(minDistance, distance)
      maxDistance = Math.max(maxDistance, distance)
    }
  }

  return {
    finite,
    filtered,
    bounds: filtered > 0 ? { minX, minY, minZ, maxX, maxY, maxZ } : null,
    minDistance,
    maxDistance,
  }
}

function applyColorMode() {
  if (!material) return
  const usesVertexColors = colorMode.value !== 'solid'
  if (material.vertexColors !== usesVertexColors) {
    material.vertexColors = usesVertexColors
    material.needsUpdate = true
  }
  if (!usesVertexColors) material.color.setRGB(...SOLID_POINT_COLOR)
}

function cancelPendingPointBufferUpdate() {
  if (pointBufferUpdateFrame) cancelAnimationFrame(pointBufferUpdateFrame)
  pointBufferUpdateFrame = 0
  pendingPointBufferFrame = null
  pendingPointBufferAllowsPaused = false
}

function schedulePointBufferUpdate(frame: PointCloudFrame, allowWhilePaused = false) {
  if (!stageActive) return
  pendingPointBufferFrame = frame
  pendingPointBufferAllowsPaused = allowWhilePaused
  if (pointBufferUpdateFrame) return

  /* 同一显示帧只把最后到达的一包写入 GPU 缓冲。 */
  pointBufferUpdateFrame = requestAnimationFrame(() => {
    pointBufferUpdateFrame = 0
    const nextFrame = pendingPointBufferFrame
    const allowsPaused = pendingPointBufferAllowsPaused
    pendingPointBufferFrame = null
    pendingPointBufferAllowsPaused = false
    if (!stageActive || !nextFrame || (paused.value && !allowsPaused)) return

    visibleFrame = nextFrame
    updatePointBuffer(nextFrame)
    publishDiagnostics()
  })
}

function refreshVisiblePointBuffer() {
  const frame = paused.value ? visibleFrame : latestFrame ?? visibleFrame
  if (!frame) return
  schedulePointBufferUpdate(frame, paused.value)
}

function updatePointBuffer(frame: PointCloudFrame) {
  cachedSnapshot.value = false
  const available = Math.max(0, Math.min(Math.floor(frame.count), Math.floor(frame.xyz.length / 3)))
  sourcePointCount.value = available
  if (!geometry || !positionAttribute || !colorAttribute || available <= 0) {
    clearPointCloudSessionSnapshot()
    pointCount.value = 0
    validPointCount.value = 0
    filteredPointCount.value = 0
    visibleBounds = null
    geometry?.setDrawRange(0, 0)
    scheduleRender()
    return
  }

  const filter = createPointFilter()
  const usesVertexColors = colorMode.value !== 'solid'
  const usesDistanceColor = colorMode.value === 'distance'
  /* 第一遍取得真实质量读数与完整筛后范围，第二遍才均匀写入 GPU。 */
  const analysis = analyzePointFrame(frame, available, filter, usesDistanceColor)
  validPointCount.value = analysis.finite
  filteredPointCount.value = analysis.filtered
  const filteredBounds = analysis.bounds

  if (!filteredBounds) {
    clearPointCloudSessionSnapshot()
    pointCount.value = 0
    visibleBounds = null
    geometry.setDrawRange(0, 0)
    scheduleRender()
    return
  }

  const sampleCount = Math.min(analysis.filtered, pointCapacity)
  const heightRange = Math.max(0.0001, filteredBounds.maxY - filteredBounds.minY)
  const distanceRange = usesDistanceColor
    ? Math.max(0.0001, analysis.maxDistance - analysis.minDistance)
    : 1
  let visible = 0
  let filteredIndex = 0
  let nextSampleIndex = 0
  const pointColor = usesVertexColors ? new THREE.Color() : null
  const needsDistanceSquared = filter.rangeEnabled || usesDistanceColor

  for (let index = 0; index < available && visible < sampleCount; index += 1) {
    const source = index * 3
    const rosX = frame.xyz[source]!
    const rosY = frame.xyz[source + 1]!
    const rosZ = frame.xyz[source + 2]!
    if (!Number.isFinite(rosX) || !Number.isFinite(rosY) || !Number.isFinite(rosZ)) continue
    const distanceSquared = needsDistanceSquared ? rosX * rosX + rosY * rosY + rosZ * rosZ : 0
    if (!passesFilters(rosX, rosY, rosZ, filter, distanceSquared)) continue
    if (filteredIndex !== nextSampleIndex) {
      filteredIndex += 1
      continue
    }

    /* ROS: x 前 / y 左 / z 上 → Three.js: x / y 上 / z 后。 */
    const x = rosX
    const y = rosZ
    const z = -rosY
    const target = visible * 3
    positionBuffer[target] = x
    positionBuffer[target + 1] = y
    positionBuffer[target + 2] = z
    if (pointColor) {
      if (usesDistanceColor) {
        const distance = Math.sqrt(distanceSquared)
        const normalized = Math.min(1, Math.max(0, (distance - analysis.minDistance) / distanceRange))
        pointColor.setHSL(0.5 - normalized * 0.38, 0.78, 0.48)
      } else {
        const normalized = Math.min(1, Math.max(0, (y - filteredBounds.minY) / heightRange))
        pointColor.setHSL(0.62 - normalized * 0.58, 0.78, 0.5)
      }
      colorBuffer[target] = pointColor.r
      colorBuffer[target + 1] = pointColor.g
      colorBuffer[target + 2] = pointColor.b
    }

    filteredIndex += 1
    visible += 1
    if (visible < sampleCount) {
      nextSampleIndex = Math.round(visible * (analysis.filtered - 1) / (sampleCount - 1))
    }
  }

  geometry.setDrawRange(0, visible)
  positionAttribute.clearUpdateRanges()
  positionAttribute.addUpdateRange(0, visible * 3)
  positionAttribute.needsUpdate = true
  if (usesVertexColors) {
    colorAttribute.clearUpdateRanges()
    colorAttribute.addUpdateRange(0, visible * 3)
    colorAttribute.needsUpdate = true
  }
  pointCount.value = visible
  visibleBounds = filteredBounds
  updateSensorGuide()
  scheduleRender()
  if (visible > 0 && needsCameraFit) {
    fitCamera(visibleBounds)
    needsCameraFit = false
  }
  rememberVisiblePointCloud()
}

function currentDiagnostics(): PointCloudDiagnostics {
  return {
    state: state.value,
    streamName: selectedStream.value?.name || selectedStreamName.value,
    topic: selectedStream.value?.topic || '',
    displayedPointCount: pointCount.value,
    sourcePointCount: sourcePointCount.value,
    validPointCount: validPointCount.value,
    filteredPointCount: filteredPointCount.value,
    dataFps: dataFps.value,
    renderFps: renderFps.value,
    renderMode: renderMode.value,
    lastFrameAt: lastFrameAt.value,
    paused: paused.value,
    frameHeld: isFrameHeld.value,
    cachedSnapshot: cachedSnapshot.value,
    awaitingFirstFrame: isWaitingForFirstFrame.value,
    firstFrameTimedOut: isFirstFrameDelayed.value,
    error: errorMessage.value,
    maxVisiblePoints: maxVisiblePoints.value,
    colorMode: colorMode.value,
    viewPreset: activeViewPreset.value,
  }
}

function publishDiagnostics(force = false) {
  const now = Date.now()
  const remaining = 500 - (now - lastDiagnosticsAt)
  if (force || remaining <= 0) {
    clearTimeout(diagnosticsTimer)
    diagnosticsTimer = undefined
    lastDiagnosticsAt = now
    emit('diagnostics', currentDiagnostics())
    return
  }
  if (diagnosticsTimer) return
  diagnosticsTimer = setTimeout(() => {
    diagnosticsTimer = undefined
    publishDiagnostics(true)
  }, remaining)
}

function updateDataRate() {
  const now = performance.now()
  if (!dataWindowStarted) dataWindowStarted = now
  dataWindowFrames += 1
  const elapsed = now - dataWindowStarted
  if (elapsed < 500) return
  dataFps.value = dataWindowFrames * 1000 / elapsed
  dataWindowFrames = 0
  dataWindowStarted = now
}

function clearFirstFrameWait() {
  clearTimeout(firstFrameTimer)
  firstFrameTimer = undefined
  firstFrameTimedOut.value = false
}

function startFirstFrameWait(version: number) {
  clearFirstFrameWait()
  if (cachedSnapshot.value || hasReceivedFrame.value) return
  firstFrameTimer = setTimeout(() => {
    firstFrameTimer = undefined
    if (
      version !== connectionVersion
      || state.value !== 'connecting'
      || cachedSnapshot.value
      || hasReceivedFrame.value
      || !hub
    ) return
    firstFrameTimedOut.value = true
    publishDiagnostics(true)
  }, FIRST_FRAME_WAIT_TIMEOUT_MS)
}

function handleFrame(stream: string, frame: PointCloudFrame) {
  if (stream !== subscribedStream) return
  clearFirstFrameWait()
  cachedSnapshot.value = false
  latestFrame = frame
  lastFrameAt.value = Date.now()
  frameHeld.value = false
  if (!stageActive) return
  updateDataRate()
  clearTimeout(frameStaleTimer)
  frameStaleTimer = setTimeout(() => {
    dataFps.value = 0
    frameHeld.value = true
    state.value = 'connecting'
    publishDiagnostics(true)
  }, 2500)
  if (!paused.value) {
    schedulePointBufferUpdate(frame)
  }
  state.value = 'ok'
  errorMessage.value = ''
  publishDiagnostics()
}

async function stopHub() {
  cancelPendingPointBufferUpdate()
  clearTimeout(frameStaleTimer)
  frameStaleTimer = undefined
  clearFirstFrameWait()
  frameHeld.value = false
  removeFrameHandler?.()
  removeFrameHandler = null
  const current = hub
  hub = null
  subscribedStream = ''
  if (current) {
    try {
      await current.stop()
    } catch {
      /* 已断开时忽略 */
    }
  }
}

async function reloadStreams() {
  if (webglUnavailable.value && !initScene()) return
  const version = ++connectionVersion
  const nextSnapshotScope = session.value
    ? getPointCloudSnapshotScope(runtimeConfig.public.apiBase, session.value)
    : ''
  pointCloudSnapshotScope = nextSnapshotScope
  if (!nextSnapshotScope) clearPointCloudSessionSnapshot()
  else if (pointCloudSessionSnapshot.value?.scope !== nextSnapshotScope) pointCloudSessionSnapshot.value = null
  await stopHub()
  streams.value = []
  selectedStreamName.value = ''
  pointCount.value = 0
  sourcePointCount.value = 0
  validPointCount.value = 0
  filteredPointCount.value = 0
  dataFps.value = 0
  lastFrameAt.value = 0
  frameHeld.value = false
  cachedSnapshot.value = false
  latestFrame = null
  visibleFrame = null
  visibleBounds = null
  geometry?.setDrawRange(0, 0)
  scheduleRender()
  dataWindowStarted = 0
  dataWindowFrames = 0
  errorMessage.value = ''

  if (!session.value) {
    state.value = 'off'
    errorMessage.value = '请先登录。'
    return
  }

  loading.value = true
  state.value = 'connecting'
  try {
    const api = createAuthenticatedClient()
    const list = await api.getPointCloudStreams({ signal: AbortSignal.timeout(8000) })
    if (version !== connectionVersion) return
    streams.value = list
    const initialStream = selectInitialStream(list)
    selectedStreamName.value = initialStream?.name ?? ''
    if (!initialStream) {
      state.value = 'off'
      errorMessage.value = '后端未配置点云流。'
      return
    }

    if (!preferredStreamName.value) preferredStreamName.value = initialStream.name
    clampVisiblePointLimit(initialStream)
    resetPointBuffer(maxVisiblePoints.value)
    restorePointCloudSnapshot(initialStream)
    const nextHub = api.createPointCloudHub()
    removeFrameHandler = nextHub.onFrame(handleFrame)
    nextHub.connection.onreconnecting(() => {
      if (nextHub !== hub) return
      cancelPendingPointBufferUpdate()
      if (!hasReceivedFrame.value) clearFirstFrameWait()
      state.value = 'connecting'
    })
    nextHub.connection.onreconnected(() => {
      if (nextHub !== hub) return
      cancelPendingPointBufferUpdate()
      state.value = 'connecting'
      if (!hasReceivedFrame.value) startFirstFrameWait(version)
    })
    nextHub.connection.onclose(() => {
      if (nextHub !== hub) return
      cancelPendingPointBufferUpdate()
      clearFirstFrameWait()
      frameHeld.value = hasReceivedFrame.value
      state.value = 'off'
      errorMessage.value = '点云连接已断开。'
      publishDiagnostics(true)
    })

    await nextHub.start()
    if (version !== connectionVersion) {
      await nextHub.stop().catch(() => {})
      return
    }
    hub = nextHub
    /* 先登记当前流，再发起订阅，避免首帧在 await 期间被丢弃。 */
    subscribedStream = selectedStreamName.value
    await nextHub.subscribe(subscribedStream)
    /* 订阅成功不等于收到真实帧，等 handleFrame 后再标记 LIVE。 */
    state.value = 'connecting'
    startFirstFrameWait(version)
  } catch (error) {
    if (version !== connectionVersion) return
    state.value = 'off'
    errorMessage.value = describeError(error)
    await stopHub()
    frameHeld.value = hasReceivedFrame.value
  } finally {
    if (version === connectionVersion) loading.value = false
  }
}

async function changeStream() {
  if (!hub || !selectedStream.value || loading.value) return
  loading.value = true
  state.value = 'connecting'
  errorMessage.value = ''
  clearTimeout(frameStaleTimer)
  frameStaleTimer = undefined
  clearFirstFrameWait()
  cancelPendingPointBufferUpdate()
  clearPointCloudSessionSnapshot()
  try {
    const nextStream = selectedStream.value
    const nextStreamName = nextStream.name
    preferredStreamName.value = nextStreamName
    const previousStreamName = subscribedStream
    subscribedStream = ''
    if (previousStreamName) await hub.unsubscribe(previousStreamName)
    latestFrame = null
    visibleFrame = null
    sourcePointCount.value = 0
    validPointCount.value = 0
    filteredPointCount.value = 0
    dataFps.value = 0
    lastFrameAt.value = 0
    frameHeld.value = false
    clampVisiblePointLimit(nextStream)
    resetPointBuffer(maxVisiblePoints.value)
    subscribedStream = nextStreamName
    await hub.subscribe(nextStreamName)
    state.value = 'connecting'
    startFirstFrameWait(connectionVersion)
  } catch (error) {
    subscribedStream = ''
    state.value = 'off'
    errorMessage.value = describeError(error)
  } finally {
    loading.value = false
  }
}

function refit() {
  needsCameraFit = true
  if (visibleBounds) {
    fitCamera(visibleBounds)
    needsCameraFit = false
    return
  }
  if (pointCount.value === 0 && camera && controls) {
    camera.position.set(5, 3.5, 5)
    controls.target.set(0, 0.5, 0)
    controls.update()
  }
  scheduleRender()
}

function togglePaused() {
  paused.value = !paused.value
  if (paused.value) cancelPendingPointBufferUpdate()
  else refreshVisiblePointBuffer()
  publishDiagnostics(true)
}

function exportMetadata() {
  const stream = selectedStream.value
  const clean = (value: string) => value.replace(/[\r\n]+/g, ' ')
  const range = rangeFilterEnabled.value
    ? `${rangeMinMeters.value.toFixed(2)}-${rangeMaxMeters.value.toFixed(2)}m`
    : 'off'
  const height = heightFilterEnabled.value
    ? `${heightMinMeters.value.toFixed(2)}-${heightMaxMeters.value.toFixed(2)}m`
    : 'off'
  return [
    `generated_at=${new Date().toISOString()}`,
    `stream=${clean(stream?.name || 'unknown')}`,
    `topic=${clean(stream?.topic || 'unknown')}`,
    'coordinate_space=ros_xyz',
    `view_preset=${activeViewPreset.value}`,
    `source_points=${sourcePointCount.value}`,
    `finite_points=${validPointCount.value}`,
    `filtered_points=${filteredPointCount.value}`,
    `visible_points=${pointCount.value}`,
    `range_filter=${range}`,
    `height_filter=${height}`,
  ]
}

function downloadVisiblePoints() {
  if (pointCount.value <= 0) return
  const lines: string[] = []
  const metadata = exportMetadata()
  if (exportFormat.value === 'ply') {
    lines.push(
      'ply',
      'format ascii 1.0',
      ...metadata.map(item => `comment ${item}`),
      `element vertex ${pointCount.value}`,
      'property float x',
      'property float y',
      'property float z',
      'end_header',
    )
  } else {
    lines.push(...metadata.map(item => `# ${item}`), 'x,y,z')
  }

  for (let index = 0; index < pointCount.value; index += 1) {
    const offset = index * 3
    const x = positionBuffer[offset]!
    const y = -positionBuffer[offset + 2]!
    const z = positionBuffer[offset + 1]!
    lines.push(exportFormat.value === 'csv'
      ? `${x.toFixed(6)},${y.toFixed(6)},${z.toFixed(6)}`
      : `${x.toFixed(6)} ${y.toFixed(6)} ${z.toFixed(6)}`)
  }

  const extension = exportFormat.value
  const mimeType = extension === 'csv' ? 'text/csv;charset=utf-8' : 'text/plain;charset=utf-8'
  const url = URL.createObjectURL(new Blob([lines.join('\n')], { type: mimeType }))
  const anchor = document.createElement('a')
  const streamName = (selectedStream.value?.name || 'pointcloud').replace(/[^a-z0-9_-]+/gi, '-')
  anchor.href = url
  anchor.download = `${streamName}-${new Date().toISOString().replace(/[:.]/g, '-')}.${extension}`
  document.body.appendChild(anchor)
  anchor.click()
  anchor.remove()
  setTimeout(() => URL.revokeObjectURL(url), 0)
}

function disposeScene() {
  cancelAnimationFrame(animationFrame)
  animationFrame = 0
  cancelPendingPointBufferUpdate()
  clearTimeout(diagnosticsTimer)
  clearTimeout(frameStaleTimer)
  clearTimeout(firstFrameTimer)
  clearTimeout(renderIdleTimer)
  diagnosticsTimer = undefined
  frameStaleTimer = undefined
  firstFrameTimer = undefined
  renderIdleTimer = undefined
  resizeObserver?.disconnect()
  resizeObserver = null
  controls?.removeEventListener('change', scheduleRender)
  controls?.removeEventListener('start', handleControlsStart)
  controls?.dispose()
  disposeSensorGuide()
  geometry?.dispose()
  material?.dispose()
  renderer?.dispose()
  rendererCanvas?.removeEventListener('webglcontextlost', onWebglContextLost)
  rendererCanvas?.removeEventListener('webglcontextcreationerror', onWebglContextCreationError)
  controls = null
  geometry = null
  material = null
  renderer = null
  rendererCanvas = null
  scene = null
  camera = null
  gridHelper = null
  axesHelper = null
  positionAttribute = null
  colorAttribute = null
  visibleBounds = null
  renderFps.value = 0
  renderMode.value = 'idle'
}

function suspendPointCloudStage() {
  stageActive = false
  cancelAnimationFrame(animationFrame)
  animationFrame = 0
  cancelPendingPointBufferUpdate()
  clearTimeout(renderIdleTimer)
  renderIdleTimer = undefined
  renderFps.value = 0
  renderMode.value = 'idle'
  publishDiagnostics(true)
}

function resumePointCloudStage() {
  stageActive = true
  if (webglUnavailable.value || !renderer) return
  if (!paused.value && latestFrame) schedulePointBufferUpdate(latestFrame)
  scheduleRender()
  publishDiagnostics(true)
}

onMounted(() => {
  if (!initScene()) return
  stopSessionWatch = watch(() => session.value?.token, () => void reloadStreams(), { immediate: true })
})

onActivated(() => {
  resumePointCloudStage()
})

onDeactivated(() => {
  suspendPointCloudStage()
})

onUnmounted(() => {
  stageActive = false
  connectionVersion += 1
  stopSessionWatch?.()
  if (!session.value) clearPointCloudSessionSnapshot()
  void stopHub()
  disposeScene()
  state.value = 'off'
  emit('state', 'off')
  emit('diagnostics', currentDiagnostics())
})
</script>

<template>
  <section ref="hostEl" class="pointcloud-stage" aria-label="机器人近距感知与空间扫描视图">
    <canvas ref="canvasEl" class="pointcloud-canvas"></canvas>

    <div v-if="selectedStream" class="cloud-scene-frame" aria-hidden="true">
      <span class="cloud-scene-corner is-tl"></span>
      <span class="cloud-scene-corner is-tr"></span>
      <span class="cloud-scene-corner is-br"></span>
      <span class="cloud-scene-corner is-bl"></span>
    </div>

    <div
      v-if="selectedStream"
      class="cloud-readout hud-mono"
      :class="{ 'is-held': isFrameHeld, 'is-cached': cachedSnapshot, 'is-paused': paused }"
      aria-live="polite"
    >
      <span class="cloud-readout-dot" :class="`is-${cloudStateTone}`" aria-hidden="true"></span>
      <span class="cloud-readout-kicker">{{ spatialSceneLabel }}</span>
      <span class="cloud-readout-meta">{{ spatialSceneMeta }}</span>
      <span class="cloud-readout-status">{{ cloudStateLabel }}</span>
      <span class="cloud-readout-mode">{{ viewPresetLabel }}</span>
    </div>

    <Teleport defer to="#media-drawer-content">
      <section class="media-toolbar cloud-toolbar" aria-label="点云设置">
        <div class="media-toolbar-head">
          <div class="media-toolbar-heading">
            <span class="media-status-dot" :class="`is-${cloudStateTone}`" aria-hidden="true"></span>
            <span class="media-toolbar-title hud-mono">SPATIAL SENSE</span>
            <span class="media-toolbar-state hud-mono">{{ cloudStateLabel }}</span>
          </div>
          <div class="media-toolbar-actions">
            <button
              type="button"
              :title="paused ? '恢复点云画面' : '暂停点云画面'"
              :aria-label="paused ? '恢复点云画面' : '暂停点云画面'"
              :aria-pressed="paused"
              :disabled="state !== 'ok'"
              @click="togglePaused"
            >
              <Icon :name="paused ? 'lucide:play' : 'lucide:pause'" size="15" />
            </button>
            <button type="button" title="适配点云视图" aria-label="适配点云视图" @click="refit">
              <Icon name="lucide:focus" size="15" />
            </button>
            <button type="button" title="重新连接点云" aria-label="重新连接点云" :disabled="loading" @click="reloadStreams">
              <Icon :class="{ 'media-spin': loading }" name="lucide:refresh-cw" size="15" />
            </button>
          </div>
        </div>
        <label class="media-field">
          <span class="media-field-label">数据流</span>
          <select v-model="selectedStreamName" :disabled="loading || streams.length === 0" @change="changeStream">
            <option v-if="streams.length === 0" value="">无点云流</option>
            <option v-for="stream in streams" :key="stream.name" :value="stream.name">
              {{ stream.name }} · {{ stream.topic }}
            </option>
          </select>
        </label>
        <label class="media-field">
          <span class="media-field-label">观察</span>
          <select v-model="viewPreset" @change="applyViewPreset">
            <option value="auto">自动（/scan 近距态势）</option>
            <option value="scan-top">/scan 近距态势</option>
            <option value="points-3d">points2 空间扫描</option>
            <option v-if="viewPreset === 'custom'" value="custom" disabled>CUSTOM</option>
          </select>
        </label>
        <label class="media-field">
          <span class="media-field-label">着色</span>
          <select v-model="colorMode">
            <option value="solid">单色</option>
            <option value="height">按高度</option>
            <option value="distance">按距离</option>
          </select>
        </label>
        <label class="cloud-range">
          <span class="media-field-label">点大小</span>
          <input
            v-model.number="pointSize"
            type="range"
            :min="POINT_SIZE_MIN"
            :max="POINT_SIZE_MAX"
            :step="POINT_SIZE_STEP"
          />
          <strong class="hud-mono">{{ pointSize.toFixed(3) }}</strong>
        </label>
        <label class="cloud-range">
          <span class="media-field-label">显示上限</span>
          <input
            v-model.number="maxVisiblePoints"
            type="range"
            :min="pointLimitMin"
            :max="streamPointLimit"
            :step="pointLimitStep"
          />
          <strong class="hud-mono">{{ maxVisiblePoints.toLocaleString() }}</strong>
        </label>
        <div class="cloud-filters" role="group" aria-label="点云筛选">
          <div class="cloud-filter-row">
            <label class="cloud-filter-toggle">
              <input v-model="rangeFilterEnabled" type="checkbox" />
              <span>距离</span>
            </label>
            <div class="cloud-filter-values" :class="{ 'is-disabled': !rangeFilterEnabled }">
              <input
                v-model.number="rangeMinMeters"
                type="number"
                min="0"
                step="0.1"
                :disabled="!rangeFilterEnabled"
                aria-label="最小距离（米）"
              />
              <span aria-hidden="true">-</span>
              <input
                v-model.number="rangeMaxMeters"
                type="number"
                min="0"
                step="0.1"
                :disabled="!rangeFilterEnabled"
                aria-label="最大距离（米）"
              />
              <em>m</em>
            </div>
          </div>
          <div class="cloud-filter-row">
            <label class="cloud-filter-toggle">
              <input v-model="heightFilterEnabled" type="checkbox" />
              <span>高度</span>
            </label>
            <div class="cloud-filter-values" :class="{ 'is-disabled': !heightFilterEnabled }">
              <input
                v-model.number="heightMinMeters"
                type="number"
                step="0.1"
                :disabled="!heightFilterEnabled"
                aria-label="最小高度（米）"
              />
              <span aria-hidden="true">-</span>
              <input
                v-model.number="heightMaxMeters"
                type="number"
                step="0.1"
                :disabled="!heightFilterEnabled"
                aria-label="最大高度（米）"
              />
              <em>m</em>
            </div>
          </div>
        </div>
        <div class="cloud-toggles" role="group" aria-label="点云辅助显示">
          <label>
            <input v-model="showGrid" type="checkbox" />
            <span>参考网格</span>
          </label>
          <label>
            <input v-model="showAxes" type="checkbox" />
            <span>坐标参考</span>
          </label>
        </div>
        <div class="cloud-export">
          <select v-model="exportFormat" aria-label="点云导出格式">
            <option value="ply">ASCII PLY</option>
            <option value="csv">CSV</option>
          </select>
          <button
            type="button"
            title="导出当前可见点"
            aria-label="导出当前可见点"
            :disabled="pointCount === 0"
            @click="downloadVisiblePoints"
          >
            <Icon name="lucide:download" size="15" />
          </button>
        </div>
        <div class="cloud-stats hud-mono">
          <span>{{ sourcePointCount.toLocaleString() }} → {{ pointCount.toLocaleString() }} PTS</span>
          <span>DATA {{ dataFps > 0 ? dataFps.toFixed(1) : '--' }}</span>
          <span>{{ renderMode === 'idle' ? 'DRAW IDLE' : `GPU ${renderFps > 0 ? renderFps.toFixed(0) : '--'}` }}</span>
        </div>
        <div class="cloud-quality hud-mono">
          <span>VALID {{ sourcePointCount > 0 ? `${validPointRatio.toFixed(1)}%` : '--' }}</span>
          <span>CUT {{ filteredOutPointCount.toLocaleString() }}</span>
          <span>{{ viewPresetLabel }}</span>
        </div>
      </section>
    </Teleport>

    <div v-if="errorMessage && !cachedSnapshot && pointCount === 0 && !isFrameHeld" class="cloud-empty" role="status">
      <Icon name="lucide:cloud-off" size="22" />
      <strong>{{ errorMessage }}</strong>
    </div>
    <div v-else-if="isFirstFrameDelayed" class="cloud-empty is-passive cloud-empty-no-frame" role="status">
      <Icon name="lucide:radio-tower" size="22" />
      <strong>PointCloudHub 已连接，未收到点云帧</strong>
      <small>{{ selectedStream?.name }} · {{ selectedStream?.topic }}</small>
    </div>
    <div v-else-if="isWaitingForFirstFrame" class="cloud-empty" role="status">
      <Icon class="media-spin" name="lucide:loader-circle" size="22" />
      <strong>正在连接感知流</strong>
    </div>
    <div v-else-if="hasReceivedFrame && sourcePointCount === 0" class="cloud-empty is-passive" role="status">
      <Icon name="lucide:scan" size="22" />
      <strong>当前帧没有可用点</strong>
    </div>
    <div v-else-if="sourcePointCount > 0 && pointCount === 0" class="cloud-empty is-passive" role="status">
      <Icon name="lucide:scan" size="22" />
      <strong>当前筛选未留下可见点</strong>
    </div>
    <div v-if="cachedSnapshot" class="cloud-frame-hold is-cached hud-mono" role="status">
      <Icon name="lucide:history" size="12" />
      <span>已恢复最近感知帧</span>
      <small>仅本次页面会话</small>
    </div>
    <div v-else-if="isFrameHeld" class="cloud-frame-hold hud-mono" role="status">
      <Icon name="lucide:pause" size="12" />
      <span>感知帧暂未更新</span>
      <small>{{ errorMessage || '保留最后画面' }}</small>
    </div>
  </section>
</template>

<style>
.pointcloud-stage {
  position: absolute;
  inset: 0;
  overflow: hidden;
  background: #f1f5f2;
}

.pointcloud-canvas {
  display: block;
  width: 100%;
  height: 100%;
  touch-action: none;
}

.cloud-scene-frame {
  position: absolute;
  inset: 12px;
  pointer-events: none;
  animation: cloud-scene-frame-in 520ms cubic-bezier(0.22, 1, 0.36, 1) both;
}

.cloud-scene-corner {
  position: absolute;
  width: 28px;
  height: 28px;
  border-color: rgba(15, 118, 110, 0.4);
  border-style: solid;
}

.cloud-scene-corner.is-tl {
  top: 0;
  left: 0;
  border-width: 1px 0 0 1px;
}

.cloud-scene-corner.is-tr {
  top: 0;
  right: 0;
  border-width: 1px 1px 0 0;
}

.cloud-scene-corner.is-br {
  right: 0;
  bottom: 0;
  border-width: 0 1px 1px 0;
}

.cloud-scene-corner.is-bl {
  bottom: 0;
  left: 0;
  border-width: 0 0 1px 1px;
}

.cloud-readout {
  position: absolute;
  top: 18px;
  left: 18px;
  display: inline-flex;
  align-items: center;
  gap: 7px;
  max-width: min(440px, calc(100vw - 150px));
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

.cloud-readout.is-held {
  border-top-color: rgba(217, 119, 6, 0.55);
  border-left-color: rgba(217, 119, 6, 0.65);
  background: rgba(255, 251, 235, 0.8);
  color: #8a5a13;
}

.cloud-readout.is-cached {
  border-top-color: rgba(37, 99, 235, 0.5);
  border-left-color: rgba(37, 99, 235, 0.62);
  background: rgba(239, 246, 255, 0.8);
  color: #1d4ed8;
}

.cloud-readout.is-paused {
  border-top-color: rgba(37, 99, 235, 0.44);
  border-left-color: rgba(37, 99, 235, 0.54);
}

.cloud-readout-dot {
  width: 5px;
  height: 5px;
  flex: none;
  border-radius: 50%;
  background: #94a3b8;
  box-shadow: 0 0 0 4px rgba(148, 163, 184, 0.12);
}

.cloud-readout-dot.is-ok {
  background: #0f766e;
  box-shadow: 0 0 0 4px rgba(13, 148, 136, 0.12);
}

.cloud-readout-dot.is-connecting {
  background: #d97706;
  box-shadow: 0 0 0 4px rgba(217, 119, 6, 0.12);
}

.cloud-readout-dot.is-held {
  background: #d97706;
  box-shadow: 0 0 0 4px rgba(217, 119, 6, 0.12);
}

.cloud-readout-dot.is-cached {
  background: #2563eb;
  box-shadow: 0 0 0 4px rgba(37, 99, 235, 0.12);
}

.cloud-readout-kicker {
  color: #0f766e;
  font-size: 8px;
}

.cloud-readout-meta {
  flex: 1 1 auto;
  min-width: 0;
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}

.cloud-readout-status {
  color: #64748b;
  font-size: 7.5px;
  white-space: nowrap;
}

.cloud-readout.is-held .cloud-readout-kicker,
.cloud-readout.is-held .cloud-readout-status {
  color: #a16207;
}

.cloud-readout.is-cached .cloud-readout-kicker,
.cloud-readout.is-cached .cloud-readout-status {
  color: #1d4ed8;
}

.cloud-readout-mode {
  padding-left: 7px;
  border-left: 1px solid rgba(82, 104, 106, 0.28);
  color: #64748b;
}

.cloud-toolbar .media-status-dot.is-held {
  background: #d97706;
  box-shadow: 0 0 0 4px rgba(217, 119, 6, 0.12);
}

.cloud-toolbar .media-status-dot.is-cached {
  background: #2563eb;
  box-shadow: 0 0 0 4px rgba(37, 99, 235, 0.12);
}

@keyframes cloud-scene-frame-in {
  from {
    opacity: 0;
    transform: scale(0.986);
  }
  to {
    opacity: 1;
    transform: scale(1);
  }
}

.cloud-count {
  color: var(--muted-foreground);
  font-size: 9px;
  text-align: left;
  white-space: nowrap;
}

.cloud-range {
  display: grid;
  grid-template-columns: 64px minmax(0, 1fr) 46px;
  align-items: center;
  gap: 7px;
}

.cloud-range input[type='range'] {
  width: 100%;
  min-width: 0;
  accent-color: var(--primary);
}

.cloud-range strong {
  color: var(--foreground);
  font-size: 8.5px;
  font-weight: 500;
  text-align: right;
}

.cloud-filters {
  display: grid;
  gap: 5px;
  padding-top: 7px;
  border-top: 1px solid color-mix(in srgb, var(--border) 72%, transparent);
}

.cloud-filter-row {
  display: grid;
  grid-template-columns: 58px minmax(0, 1fr);
  align-items: center;
  gap: 7px;
  min-height: 28px;
}

.cloud-filter-toggle {
  display: inline-flex;
  align-items: center;
  gap: 6px;
  min-width: 0;
  color: var(--muted-foreground);
  font-size: 8.5px;
  cursor: pointer;
}

.cloud-filter-toggle input {
  width: 12px;
  height: 12px;
  margin: 0;
  accent-color: var(--primary);
}

.cloud-filter-values {
  display: grid;
  grid-template-columns: minmax(0, 1fr) 8px minmax(0, 1fr) 10px;
  align-items: center;
  gap: 4px;
  min-width: 0;
  color: var(--ornament);
  font-size: 8px;
}

.cloud-filter-values input {
  width: 100%;
  min-width: 0;
  height: 28px;
  padding: 0 5px;
  border: 1px solid var(--border);
  border-radius: 3px;
  background: color-mix(in srgb, var(--background) 68%, transparent);
  color: var(--foreground);
  font: inherit;
  font-variant-numeric: tabular-nums;
}

.cloud-filter-values.is-disabled {
  opacity: 0.48;
}

.cloud-filter-values em {
  font-style: normal;
  text-align: right;
}

.cloud-toggles {
  display: grid;
  grid-template-columns: repeat(2, minmax(0, 1fr));
  gap: 5px;
}

.cloud-toggles label {
  display: flex;
  align-items: center;
  gap: 6px;
  min-height: 28px;
  padding: 0 7px;
  border: 1px solid var(--border);
  color: var(--muted-foreground);
  font-size: 8.5px;
  cursor: pointer;
}

.cloud-toggles input {
  width: 12px;
  height: 12px;
  accent-color: var(--primary);
}

.cloud-export {
  display: flex;
  align-items: center;
  gap: 5px;
}

.cloud-export select {
  height: 30px;
}

.cloud-stats {
  display: grid;
  grid-template-columns: minmax(0, 1fr) auto auto;
  align-items: center;
  gap: 6px;
  color: var(--muted-foreground);
  font-size: 7.5px;
}

.cloud-stats span,
.cloud-quality span {
  min-width: 0;
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}

.cloud-quality {
  display: grid;
  grid-template-columns: repeat(3, minmax(0, 1fr));
  gap: 6px;
  color: var(--ornament);
  font-size: 7.5px;
}

.cloud-quality span:nth-child(2) {
  text-align: center;
}

.cloud-quality span:last-child {
  text-align: right;
}

.cloud-empty {
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

.cloud-empty strong {
  max-width: min(360px, 80vw);
  color: var(--foreground);
  font-size: 13px;
  font-weight: 500;
  overflow-wrap: anywhere;
}

.cloud-empty small {
  color: #71807f;
  font-size: 9px;
  letter-spacing: 0.04em;
}

.cloud-empty.is-passive {
  opacity: 0.64;
}

.cloud-empty-no-frame {
  color: #9a670d;
}

.cloud-frame-hold {
  position: absolute;
  top: 54px;
  left: 18px;
  display: inline-flex;
  align-items: center;
  gap: 6px;
  padding: 5px 8px;
  border-left: 1px solid rgba(217, 119, 6, 0.7);
  background: rgba(255, 251, 235, 0.68);
  color: #8a5a13;
  font-size: 8px;
  letter-spacing: 0.04em;
  pointer-events: none;
}

.cloud-frame-hold small {
  padding-left: 6px;
  border-left: 1px solid rgba(138, 90, 19, 0.22);
  color: #a16207;
  font-size: 7px;
}

.cloud-frame-hold.is-cached {
  border-left-color: rgba(37, 99, 235, 0.72);
  background: rgba(239, 246, 255, 0.72);
  color: #1d4ed8;
}

.cloud-frame-hold.is-cached small {
  border-left-color: rgba(29, 78, 216, 0.22);
  color: #2563eb;
}

@media (max-width: 520px) {
  .cloud-scene-frame {
    inset: 8px;
  }

  .cloud-scene-corner {
    width: 20px;
    height: 20px;
  }

  .cloud-readout {
    top: 12px;
    left: 12px;
    max-width: calc(100vw - 120px);
    gap: 5px;
  }

  .cloud-readout-mode {
    display: none;
  }

  .cloud-frame-hold {
    top: 48px;
    left: 12px;
  }
}

@media (prefers-reduced-motion: reduce) {
  .cloud-scene-frame {
    animation: none;
  }
}

</style>
