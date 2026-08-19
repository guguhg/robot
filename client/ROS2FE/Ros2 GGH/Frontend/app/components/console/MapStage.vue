<script setup lang="ts">
import type {
  CostmapFrame,
  MapFrame,
  RobotPose,
} from '@/lib/cwebapi/cwebapi-client'
import type { LinkState } from '@/composables/useRobot'
import {
  TELEMETRY_STALE_AFTER_MS,
  type FrameMetric,
  type MapFrameSource,
  type NavigationPose,
  type TelemetryMetricKey,
} from '@/composables/useTelemetry'

type LayerKey = 'globalCostmap' | 'localCostmap' | 'particles' | 'path' | 'localPlan' | 'scan' | 'mapPatch' | 'trajectory'
type InteractionMode = 'browse' | 'goal' | 'initialPose'
type PlacementMode = Exclude<InteractionMode, 'browse'>
type SubmitState = 'idle' | 'submitting' | 'sent' | 'error'
type LayerDataState = 'fresh' | 'stale' | 'missing'
type LocalizationState = 'needsInitialPose' | 'awaitingConfirmation' | 'ready'
type MapViewPreset = 'drive' | 'debug' | 'custom'
type MapCellState = 'free' | 'unknown' | 'occupied' | 'outside' | 'unavailable'
type LandingKind = 'select' | 'initialPose' | 'goal' | 'blocked'
type BitmapKey = 'map' | 'globalCostmap' | 'localCostmap' | 'mapPatch'

interface MapEvidenceStats {
  known: number
  occupied: number
  total: number
}

interface MapBounds {
  minX: number
  minY: number
  maxX: number
  maxY: number
}

interface WorldPoint {
  x: number
  y: number
}

interface SelectionBox {
  start: WorldPoint
  end: WorldPoint
}

interface ScreenPoint {
  x: number
  y: number
}

interface LandingEffect extends WorldPoint {
  kind: LandingKind
  startedAt: number
}

interface PairStrokeStyle {
  color: string
  width: number
  pointRadius?: number
  maxPoints?: number
  haloColor?: string
  haloWidth?: number
}

type PointerAction =
  | {
    kind: 'browse'
    pointerId: number
    clientX: number
    clientY: number
    startClientX: number
    startClientY: number
    startWorld: WorldPoint
    longPress: boolean
  }
  | { kind: 'pan'; pointerId: number; x: number; y: number }
  | { kind: 'box'; pointerId: number; start: WorldPoint; end: WorldPoint }
  | {
    kind: 'pose'
    pointerId: number
    clientX: number
    clientY: number
    x: number
    y: number
    mode: PlacementMode
    autoSubmit: boolean
  }
  | null

const props = withDefaults(defineProps<{
  state?: LinkState
  map?: MapFrame | null
  mapSource?: MapFrameSource
  mapCachedAt?: number
  robotPose?: RobotPose | null
  particles?: Float32Array | null
  path?: Float32Array | null
  localPlan?: Float32Array | null
  scan?: Float32Array | null
  globalCostmap?: CostmapFrame | null
  localCostmap?: CostmapFrame | null
  mapPatch?: CostmapFrame | null
  telemetryMetrics?: Record<TelemetryMetricKey, FrameMetric>
  error?: string
  mapIssue?: string
  canNavigate?: boolean
  setGoal?: (pose: NavigationPose) => Promise<void>
  setInitialPose?: (pose: NavigationPose) => Promise<void>
}>(), {
  state: 'off',
  map: null,
  mapSource: 'none',
  mapCachedAt: 0,
  robotPose: null,
  particles: null,
  path: null,
  localPlan: null,
  scan: null,
  globalCostmap: null,
  localCostmap: null,
  mapPatch: null,
  error: '',
  mapIssue: '',
  canNavigate: false,
})

const canvasEl = ref<HTMLCanvasElement | null>(null)
const hostEl = ref<HTMLElement | null>(null)
const DEFAULT_LAYER_VISIBILITY: Record<LayerKey, boolean> = {
  globalCostmap: false,
  localCostmap: false,
  particles: false,
  path: true,
  localPlan: true,
  scan: false,
  mapPatch: false,
  trajectory: true,
}
const MAP_VIEW_PRESETS: Record<Exclude<MapViewPreset, 'custom'>, Record<LayerKey, boolean>> = {
  drive: DEFAULT_LAYER_VISIBILITY,
  debug: {
    globalCostmap: true,
    localCostmap: true,
    particles: true,
    path: true,
    localPlan: true,
    scan: true,
    mapPatch: true,
    trajectory: true,
  },
}
const layers = useState<Record<LayerKey, boolean>>('console:map-layers', () => ({ ...DEFAULT_LAYER_VISIBILITY }))
for (const key of Object.keys(DEFAULT_LAYER_VISIBILITY) as LayerKey[]) {
  if (typeof layers.value[key] !== 'boolean') layers.value[key] = DEFAULT_LAYER_VISIBILITY[key]
}
/* 仅影响态势场显示，不得进入占据判断、落点拦截或导航。 */
const situationFieldEnabled = useState<boolean>('console:map-situation-field', () => true)
if (typeof situationFieldEnabled.value !== 'boolean') situationFieldEnabled.value = true
const mapViewPreset = useState<MapViewPreset>('console:map-view-preset', () => 'drive')
if (!['drive', 'debug', 'custom'].includes(mapViewPreset.value)) mapViewPreset.value = 'drive'
const interactionMode = ref<InteractionMode>('browse')
const draftPose = ref<NavigationPose | null>(null)
const submitState = ref<SubmitState>('idle')
const submitMessage = ref('')
const queuedGoalDraft = ref(false)
/* 仅保留当前页面会话，不能从 RobotPose 推断定位已经可靠。 */
const localizationState = useState<LocalizationState>('console:map-localization-state', () => 'needsInitialPose')
const robotSelected = useState<boolean>('console:map-robot-selected', () => false)
const selectionBox = ref<SelectionBox | null>(null)
const followRobot = ref(false)
const trajectoryCount = ref(0)
const trackedGoal = ref<NavigationPose | null>(null)
const trackedGoalStartedAt = ref(0)
const trackedGoalLastProgressAt = ref(0)
const trackedGoalClosestDistance = ref(Infinity)
const trackedGoalMotionDetected = ref(false)
const trackedGoalStartPose = ref<RobotPose | null>(null)
const stageClock = ref(Date.now())
const mapEvidenceStats = ref<MapEvidenceStats>({ known: 0, occupied: 0, total: 0 })
const mapSceneVersion = ref(0)
const modeDefs: Array<{ key: InteractionMode; label: string; icon: string }> = [
  { key: 'browse', label: '浏览', icon: 'lucide:hand' },
  { key: 'goal', label: '目标', icon: 'lucide:map-pin' },
  { key: 'initialPose', label: '初始位姿', icon: 'lucide:locate-fixed' },
]
const viewPresetDefs = [
  { key: 'drive', label: '态势', icon: 'lucide:map', title: '操作者态势：显示真实环境证据、路径、局部规划与行驶轨迹' },
  { key: 'debug', label: '解析', icon: 'lucide:layers-3', title: '环境解析：显示风险、扫描、定位分布与环境变化' },
] as const
const layerDefs: Array<{ key: LayerKey; label: string; icon: string }> = [
  { key: 'globalCostmap', label: '路径风险', icon: 'lucide:layers-3' },
  { key: 'localCostmap', label: '近场风险', icon: 'lucide:scan-line' },
  { key: 'path', label: '任务路径', icon: 'lucide:route' },
  { key: 'localPlan', label: '即时路线', icon: 'lucide:git-branch' },
  { key: 'scan', label: '即时感知', icon: 'lucide:scan' },
  { key: 'particles', label: '定位分布', icon: 'lucide:scatter-chart' },
  { key: 'mapPatch', label: '环境变化', icon: 'lucide:refresh-cw' },
  { key: 'trajectory', label: '行驶轨迹', icon: 'lucide:route' },
]
const layerMetricKeys: Partial<Record<LayerKey, TelemetryMetricKey>> = {
  globalCostmap: 'globalCostmap',
  localCostmap: 'localCostmap',
  particles: 'particles',
  path: 'path',
  localPlan: 'localPlan',
  scan: 'scan',
  mapPatch: 'mapPatch',
}

const isCachedMap = computed(() => props.mapSource === 'cached' && Boolean(props.map))
const mapStatusTone = computed(() => isCachedMap.value ? 'cached' : props.state)
const mapCacheAge = computed(() => {
  if (!isCachedMap.value || !props.mapCachedAt) return ''
  const seconds = Math.max(0, Math.floor((stageClock.value - props.mapCachedAt) / 1000))
  if (seconds < 60) return `${seconds}s`
  if (seconds < 3600) return `${Math.floor(seconds / 60)}m`
  if (seconds < 86_400) return `${Math.floor(seconds / 3600)}h`
  return `${Math.floor(seconds / 86_400)}d`
})
const stateLabel = computed(() => {
  if (isCachedMap.value) return 'CACHED'
  if (props.error || props.mapIssue) return 'ERROR'
  if (props.state === 'connecting') return 'LINKING'
  if (!props.map) return 'WAIT'
  return 'LIVE'
})
const mapMeta = computed(() => {
  const frame = props.map
  if (!frame) return props.mapIssue || (props.state === 'ok' ? 'MapHub 已连接，等待地图帧' : '等待 MapHub 地图帧')
  const geometry = `${frame.width}×${frame.height} · ${frame.resolution.toFixed(2)} m`
  return isCachedMap.value ? `${geometry} · CACHED ${mapCacheAge.value}` : geometry
})
const mapStatusNotice = computed(() => {
  if (!props.map) return ''
  if (isCachedMap.value) {
    return props.mapIssue || props.error || '本地缓存只用于观察，等待新的 Map 地图帧。'
  }
  return props.mapIssue || props.error
})
const situationMeta = computed(() => {
  const frame = props.map
  if (!frame) return 'ENVIRONMENT · WAIT'
  const width = frame.width * frame.resolution
  const height = frame.height * frame.resolution
  return `FIELD ${width.toFixed(1)} × ${height.toFixed(1)} m`
})
const situationActionLabel = computed(() => {
  if (interactionMode.value === 'goal') return 'SET TARGET'
  if (interactionMode.value === 'initialPose') return 'ALIGN ORIGIN'
  if (localizationState.value !== 'ready') return 'ALIGN'
  return props.robotPose ? 'OPERATE' : 'OBSERVE'
})
const situationContextLabel = computed(() => {
  if (isCachedMap.value) return 'FIELD CACHED'
  if (!mapEvidenceStats.value.known) return 'NO ENV EVIDENCE'
  if (props.localPlan?.length) return 'MOTION PLAN'
  if (props.path?.length) return 'ROUTE READY'
  if (props.localCostmap) return 'NEAR SENSE'
  return 'ENV EVIDENCE'
})
const activeMode = computed(() => modeDefs.find(mode => mode.key === interactionMode.value) ?? modeDefs[0]!)
const mapViewPresetLabel = computed(() => (
  mapViewPreset.value === 'custom' ? 'CUSTOM' : mapViewPreset.value === 'drive' ? 'SITUATION' : 'ANALYSIS'
))
const localizationLabel = computed(() => {
  if (localizationState.value === 'ready') return 'NAV READY'
  if (localizationState.value === 'awaitingConfirmation') return 'LOC CHECK'
  return 'LOC SET'
})
const localizationHint = computed(() => {
  if (localizationState.value === 'ready') return '导航已解锁'
  if (localizationState.value === 'awaitingConfirmation') return '初始位姿已发送，待人工确认'
  return '待设初始位姿'
})
const navigationBlockReason = computed(() => {
  if (!props.map) return '等待地图帧'
  if (props.mapSource !== 'live') return '当前显示缓存地图，等待新的 Map 地图帧'
  if (!props.canNavigate) return '仅当前操作者可提交地图指令'
  if (props.state !== 'ok') return 'MapHub 未连接'
  if (!props.setGoal) return '地图指令入口未就绪'
  if (localizationState.value !== 'ready') {
    return localizationState.value === 'awaitingConfirmation'
      ? '初始位姿待人工确认'
      : '请先确认初始位姿'
  }
  if (submitState.value === 'submitting') return '上一条地图指令正在提交'
  return ''
})
const initialPoseBlockReason = computed(() => {
  if (!props.map) return '等待地图帧'
  if (props.mapSource !== 'live') return '当前显示缓存地图，等待新的 Map 地图帧'
  if (!props.canNavigate) return '仅当前操作者可提交地图指令'
  if (props.state !== 'ok') return 'MapHub 未连接'
  if (!props.setInitialPose) return '地图指令入口未就绪'
  if (submitState.value === 'submitting') return '上一条地图指令正在提交'
  return ''
})
const initialPoseActionTitle = computed(() => (
  initialPoseBlockReason.value || (localizationState.value === 'ready' ? '重新设置初始位姿' : '设置初始位姿')
))
const canvasTitle = computed(() => {
  if (!props.map) return '等待地图帧'
  if (props.mapSource !== 'live') return '当前显示本地缓存地图；可浏览、平移和缩放，等待实时地图帧后才可导航'
  if (interactionMode.value === 'initialPose') return '左键落点，拖拽确定初始朝向'
  if (interactionMode.value === 'goal') return '左键落点，拖拽确定目标朝向'
  if (localizationState.value !== 'ready') return '拖拽平移，滚轮缩放，双击适配；导航等待初始位姿确认'
  return '左键选中；中键拖拽平移；长按左键框选；右键落点，拖拽设置朝向'
})
const canSubmit = computed(() => {
  if (!draftPose.value || interactionMode.value === 'browse') return false
  if (!isPointInMap(draftPose.value) || submitState.value === 'sent') return false
  return interactionMode.value === 'goal'
    ? !navigationBlockReason.value
    : !initialPoseBlockReason.value
})
const submitTitle = computed(() => {
  if (!draftPose.value) return '请先在地图上设置姿态'
  if (!isPointInMap(draftPose.value)) return '落点不在当前地图范围内'
  if (submitState.value === 'sent') return '该姿态已提交'
  const reason = interactionMode.value === 'goal' ? navigationBlockReason.value : initialPoseBlockReason.value
  if (reason) return reason
  return interactionMode.value === 'goal' ? '提交导航目标' : '提交初始位姿'
})
const trackedGoalDistance = computed(() => {
  const goal = trackedGoal.value
  const pose = props.robotPose
  if (!goal || !pose) return null
  const distance = Math.hypot(goal.x - pose.x, goal.y - pose.y)
  return Number.isFinite(distance) ? distance : null
})
const trackedGoalEstimate = computed(() => {
  const distance = trackedGoalDistance.value
  if (distance === null) return '已提交'
  if (distance <= 0.3) return '接近目标'
  if (
    trackedGoalStartedAt.value > 0
    && stageClock.value - trackedGoalStartedAt.value >= 6000
    && stageClock.value - trackedGoalLastProgressAt.value >= 6000
  ) return '疑似停滞'
  if (trackedGoalMotionDetected.value) return '检测到移动'
  return '已提交'
})

function formatPoseValue(value: number, digits = 2) {
  return Number.isFinite(value) ? value.toFixed(digits) : '--'
}

function isModeDisabled(mode: typeof modeDefs[number]) {
  if (mode.key === 'browse') return false
  if (mode.key === 'initialPose') return Boolean(initialPoseBlockReason.value)
  return Boolean(navigationBlockReason.value)
}

function modeTitle(mode: typeof modeDefs[number]) {
  if (mode.key === 'browse') return '浏览地图'
  if (mode.key === 'initialPose') return initialPoseActionTitle.value
  return navigationBlockReason.value || '目标模式（移动端备用）'
}

let context: CanvasRenderingContext2D | null = null
let resizeObserver: ResizeObserver | null = null
let drawFrame = 0
let bitmapRebuildFrame = 0
let stageActive = true
let mapKeyListenerActive = false
let devicePixelRatioValue = 1
let logicalWidth = 1
let logicalHeight = 1
let mapBitmap: HTMLCanvasElement | null = null
let situationEvidenceBitmap: HTMLCanvasElement | null = null
let globalCostmapBitmap: HTMLCanvasElement | null = null
let localCostmapBitmap: HTMLCanvasElement | null = null
let mapPatchBitmap: HTMLCanvasElement | null = null
let mapBounds: MapBounds | null = null
let mapDataSignature = ''
let mapSceneGeometrySignature = ''
let fittedMapGeometrySignature = ''
let pointerAction: PointerAction = null
let longPressTimer: ReturnType<typeof setTimeout> | undefined
let reduceMotion = false
let stageClockTimer: ReturnType<typeof setInterval> | undefined
const trajectory: Array<{ x: number; y: number }> = []
const landingEffects: LandingEffect[] = []
const dirtyBitmaps = new Set<BitmapKey>()
let lastSubmittedGoal: NavigationPose | null = null
let lastGoalSubmittedAt = 0
const MAX_TRAJECTORY_POINTS = 600
const POINTER_DRAG_THRESHOLD = 6
const ROBOT_HIT_RADIUS_PX = 22
const LONG_PRESS_DELAY_MS = 280
const GOAL_DEDUP_WINDOW_MS = 900
const OCCUPIED_CELL_THRESHOLD = 65
const MAX_SCAN_RENDER_POINTS = 2_800
const MAX_PARTICLE_RENDER_POINTS = 1_800
const MAX_PATH_RENDER_POINTS = 4_000
const MAX_LOCAL_PLAN_RENDER_POINTS = 2_400
const STALE_LAYER_OPACITY = 0.34
const view = reactive({ centerX: 0, centerY: 0, scale: 40 })

function validDimension(value: number) {
  return Number.isInteger(value) && value > 0 && value <= 4096
}

function createBitmap(
  width: number,
  height: number,
  values: ArrayLike<number>,
  color: (value: number) => [number, number, number, number],
) {
  if (!validDimension(width) || !validDimension(height) || width * height > 16_000_000) return null
  const bitmap = document.createElement('canvas')
  bitmap.width = width
  bitmap.height = height
  const bitmapContext = bitmap.getContext('2d')
  if (!bitmapContext) return null

  const image = bitmapContext.createImageData(width, height)
  for (let index = 0; index < width * height; index += 1) {
    const [r, g, b, a] = color(Number(values[index] ?? -1))
    const offset = index * 4
    image.data[offset] = r
    image.data[offset + 1] = g
    image.data[offset + 2] = b
    image.data[offset + 3] = a
  }
  bitmapContext.putImageData(image, 0, 0)
  return bitmap
}

function createMapBitmap(frame: MapFrame | null) {
  if (
    !frame
    || !Number.isFinite(frame.resolution)
    || frame.resolution <= 0
    || !validDimension(frame.width)
    || !validDimension(frame.height)
    || frame.width * frame.height > 16_000_000
  ) return null
  return createBitmap(frame.width, frame.height, frame.data, (input) => {
    const value = Number(input)
    if (!Number.isFinite(value) || value < 0) return [202, 211, 211, 168]
    if (value <= 5) return [247, 249, 245, 250]
    if (value >= OCCUPIED_CELL_THRESHOLD) return [43, 59, 62, 244]
    const density = Math.min(1, Math.max(0, value / OCCUPIED_CELL_THRESHOLD))
    return [
      Math.round(244 - density * 74),
      Math.round(247 - density * 68),
      Math.round(242 - density * 64),
      238,
    ]
  })
}

/*
 * 态势层只呈现 MapFrame 中已经观测到的证据：已知区域给出轻微底色，
 * 占据值给出较强标记。这里不闭合边界、不填补未知格，也不生成可通行推断。
 */
function createSituationEvidenceBitmap(frame: MapFrame | null) {
  const empty = { bitmap: null, known: 0, occupied: 0 }
  if (
    !frame
    || !Number.isFinite(frame.resolution)
    || frame.resolution <= 0
    || !validDimension(frame.width)
    || !validDimension(frame.height)
    || frame.width * frame.height > 16_000_000
  ) return empty

  let known = 0
  let occupied = 0
  const bitmap = createBitmap(frame.width, frame.height, frame.data, (input) => {
    const value = Number(input)
    if (!Number.isFinite(value) || value < 0) return [0, 0, 0, 0]
    known += 1
    if (value >= OCCUPIED_CELL_THRESHOLD) {
      occupied += 1
      const confidence = Math.min(1, Math.max(0, (value - OCCUPIED_CELL_THRESHOLD) / (100 - OCCUPIED_CELL_THRESHOLD)))
      return [48, 67, 65, 176 + Math.round(confidence * 48)]
    }
    if (value <= 5) return [216, 232, 223, 142]
    const confidence = Math.min(1, Math.max(0, (value - 5) / (OCCUPIED_CELL_THRESHOLD - 5)))
    return [
      Math.round(193 - confidence * 64),
      Math.round(211 - confidence * 60),
      Math.round(201 - confidence * 60),
      142 + Math.round(confidence * 44),
    ]
  })
  return { bitmap, known, occupied }
}

function createCostmapBitmap(frame: CostmapFrame | null, tone: 'amber' | 'red' | 'teal') {
  if (!frame || !Number.isFinite(frame.resolution) || frame.resolution <= 0) return null
  const rgb: [number, number, number] = tone === 'amber'
    ? [217, 119, 6]
    : tone === 'red'
      ? [220, 38, 38]
      : [13, 148, 136]
  return createBitmap(frame.width, frame.height, frame.data, value => {
    const intensity = Math.min(255, Math.max(0, value))
    if (intensity < 16) return [rgb[0], rgb[1], rgb[2], 0]
    const weight = intensity / 255
    const alpha = weight < 0.35
      ? 28 + Math.round(weight * 88)
      : 72 + Math.round(weight * 116)
    return [rgb[0], rgb[1], rgb[2], Math.min(188, alpha)]
  })
}

function localToWorld(frame: MapFrame, x: number, y: number) {
  const cosine = Math.cos(frame.originYaw)
  const sine = Math.sin(frame.originYaw)
  return {
    x: frame.originX + cosine * x - sine * y,
    y: frame.originY + sine * x + cosine * y,
  }
}

function getMapBounds(frame: MapFrame | null): MapBounds | null {
  if (!frame || !Number.isFinite(frame.resolution) || frame.resolution <= 0) return null
  const width = frame.width * frame.resolution
  const height = frame.height * frame.resolution
  const corners = [
    localToWorld(frame, 0, 0),
    localToWorld(frame, width, 0),
    localToWorld(frame, 0, height),
    localToWorld(frame, width, height),
  ]
  return corners.reduce<MapBounds>((bounds, point) => ({
    minX: Math.min(bounds.minX, point.x),
    minY: Math.min(bounds.minY, point.y),
    maxX: Math.max(bounds.maxX, point.x),
    maxY: Math.max(bounds.maxY, point.y),
  }), {
    minX: Infinity,
    minY: Infinity,
    maxX: -Infinity,
    maxY: -Infinity,
  })
}

function getMapGeometrySignature(frame: MapFrame | null) {
  if (!frame) return ''
  return [
    frame.width,
    frame.height,
    frame.resolution.toFixed(6),
    frame.originX.toFixed(6),
    frame.originY.toFixed(6),
    frame.originYaw.toFixed(6),
  ].join('|')
}

function getMapDataSignature(frame: MapFrame | null) {
  const geometry = getMapGeometrySignature(frame)
  if (!frame || !geometry) return ''
  let hash = 2_166_136_261
  const cellCount = Math.min(frame.width * frame.height, frame.data.length)
  for (let index = 0; index < cellCount; index += 1) {
    const value = Number(frame.data[index] ?? -1)
    hash = Math.imul(hash ^ ((Number.isFinite(value) ? value : -1) + 1), 16_777_619)
  }
  return `${geometry}|${frame.data.length}|${(hash >>> 0).toString(16)}`
}

function scheduleDraw() {
  if (!stageActive || drawFrame) return
  drawFrame = requestAnimationFrame(() => {
    drawFrame = 0
    if (!stageActive) return
    draw()
  })
}

function fitView() {
  if (!mapBounds || logicalWidth <= 104 || logicalHeight <= 104) return
  const width = Math.max(0.1, mapBounds.maxX - mapBounds.minX)
  const height = Math.max(0.1, mapBounds.maxY - mapBounds.minY)
  const padding = 52
  view.centerX = (mapBounds.minX + mapBounds.maxX) / 2
  view.centerY = (mapBounds.minY + mapBounds.maxY) / 2
  view.scale = Math.max(2, Math.min(480, Math.min(
    (logicalWidth - padding * 2) / width,
    (logicalHeight - padding * 2) / height,
  )))
  fittedMapGeometrySignature = getMapGeometrySignature(props.map)
  scheduleDraw()
}

function fitMap() {
  followRobot.value = false
  fitView()
}

function toggleFollowRobot() {
  if (!props.robotPose) return
  followRobot.value = !followRobot.value
  if (followRobot.value) {
    view.centerX = props.robotPose.x
    view.centerY = props.robotPose.y
    scheduleDraw()
  }
}

function clearTrajectory() {
  trajectory.length = 0
  trajectoryCount.value = 0
  scheduleDraw()
}

function hasLayerData(key: LayerKey) {
  if (key === 'globalCostmap') return Boolean(props.globalCostmap)
  if (key === 'localCostmap') return Boolean(props.localCostmap)
  if (key === 'particles') return Boolean(props.particles?.length)
  if (key === 'path') return Boolean(props.path?.length)
  if (key === 'localPlan') return Boolean(props.localPlan?.length)
  if (key === 'scan') return Boolean(props.scan?.length)
  if (key === 'mapPatch') return Boolean(props.mapPatch)
  return trajectoryCount.value > 1
}

function layerDataState(key: LayerKey): LayerDataState {
  if (!hasLayerData(key)) return 'missing'
  if (key === 'trajectory') return 'fresh'

  const metricKey = layerMetricKeys[key]
  const metric = metricKey ? props.telemetryMetrics?.[metricKey] : undefined
  if (!metric?.lastAt) return 'stale'
  return stageClock.value - metric.lastAt <= TELEMETRY_STALE_AFTER_MS[metricKey!] ? 'fresh' : 'stale'
}

function layerOpacity(key: LayerKey) {
  return layerDataState(key) === 'stale' ? STALE_LAYER_OPACITY : 1
}

function crossesLayerStaleBoundary(key: LayerKey, previous: number, next: number) {
  if (!layers.value[key] || key === 'trajectory' || !hasLayerData(key)) return false
  const metricKey = layerMetricKeys[key]
  const metric = metricKey ? props.telemetryMetrics?.[metricKey] : undefined
  if (!metric?.lastAt) return false
  const staleAt = metric.lastAt + TELEMETRY_STALE_AFTER_MS[metricKey!]
  return previous <= staleAt && next > staleAt
}

function refreshStageClock() {
  if (!stageActive) return
  const previous = stageClock.value
  const next = Date.now()
  stageClock.value = next
  if (layerDefs.some(layer => crossesLayerStaleBoundary(layer.key, previous, next))) scheduleDraw()
}

function layerToggleTitle(layer: typeof layerDefs[number]) {
  const dataState = layerDataState(layer.key)
  const stateLabel = dataState === 'fresh' ? '数据新鲜' : dataState === 'stale' ? '数据陈旧' : '暂无数据'
  return `${layer.label} · ${stateLabel}；点击${layers.value[layer.key] ? '隐藏' : '显示'}`
}

function applyMapViewPreset(preset: Exclude<MapViewPreset, 'custom'>) {
  const visibility = MAP_VIEW_PRESETS[preset]
  for (const key of Object.keys(DEFAULT_LAYER_VISIBILITY) as LayerKey[]) {
    layers.value[key] = visibility[key]
  }
  mapViewPreset.value = preset
  scheduleDraw()
}

function toggleLayer(key: LayerKey) {
  layers.value[key] = !layers.value[key]
  mapViewPreset.value = 'custom'
  scheduleDraw()
}

function setWorldTransform() {
  if (!context) return
  context.setTransform(
    devicePixelRatioValue * view.scale,
    0,
    0,
    -devicePixelRatioValue * view.scale,
    devicePixelRatioValue * (logicalWidth / 2 - view.centerX * view.scale),
    devicePixelRatioValue * (logicalHeight / 2 + view.centerY * view.scale),
  )
}

function drawRaster(
  bitmap: HTMLCanvasElement | null,
  originX: number,
  originY: number,
  resolution: number,
  width: number,
  height: number,
  originYaw = 0,
  smooth = false,
) {
  if (!context || !bitmap || !Number.isFinite(resolution) || resolution <= 0) return
  context.save()
  context.translate(originX, originY)
  context.rotate(originYaw)
  context.scale(resolution, resolution)
  context.imageSmoothingEnabled = smooth
  if (smooth) context.imageSmoothingQuality = 'medium'
  context.drawImage(bitmap, 0, 0, width, height)
  context.restore()
}

/*
 * 态势视图不尝试把稀疏占据证据还原为建筑边界。它先显示真实环境证据，
 * 再叠加覆盖范围、坐标尺度和行动信息；所有推断仍留在后端导航栈。
 */
function drawSituationNoEvidence(fieldWidth: number, fieldHeight: number, inverseScale: number, originYaw: number) {
  if (!context || mapEvidenceStats.value.known > 0) return

  /* 未知区域保持未知，只用低对比点阵说明“尚未观测”，不制造地图结构。 */
  let dotSpacing = Math.max(0.34, Math.min(0.72, 16 * inverseScale))
  const estimatedDots = (fieldWidth / dotSpacing) * (fieldHeight / dotSpacing)
  if (estimatedDots > 1_600) dotSpacing *= Math.sqrt(estimatedDots / 1_600)
  const dotRadius = 0.55 * inverseScale

  context.save()
  context.beginPath()
  context.rect(0, 0, fieldWidth, fieldHeight)
  context.clip()
  context.fillStyle = 'rgba(72, 96, 91, 0.075)'
  for (let x = dotSpacing * 0.5; x < fieldWidth; x += dotSpacing) {
    for (let y = dotSpacing * 0.5; y < fieldHeight; y += dotSpacing) {
      context.moveTo(x + dotRadius, y)
      context.arc(x, y, dotRadius, 0, Math.PI * 2)
    }
  }
  context.fill()
  context.restore()

  const compact = fieldWidth * view.scale < 190 || fieldHeight * view.scale < 86
  const labelSize = 10 * inverseScale
  const sublabelSize = 7.5 * inverseScale
  const labelY = compact ? 0 : -5 * inverseScale
  context.save()
  context.translate(fieldWidth / 2, fieldHeight / 2)
  /* 世界层翻转了 Y 轴；文字层抵消该翻转和地图朝向，保持屏幕阅读方向。 */
  context.rotate(-originYaw)
  context.scale(1, -1)
  context.textAlign = 'center'
  context.textBaseline = 'middle'
  context.fillStyle = 'rgba(39, 61, 61, 0.76)'
  context.font = `600 ${labelSize}px ui-monospace, SFMono-Regular, Menlo, monospace`
  context.fillText('NO ENV EVIDENCE', 0, labelY)
  if (!compact) {
    context.fillStyle = 'rgba(88, 107, 102, 0.66)'
    context.font = `500 ${sublabelSize}px ui-monospace, SFMono-Regular, Menlo, monospace`
    context.fillText('MAP FRAME RECEIVED · WAITING FOR KNOWN CELLS', 0, labelY + 14 * inverseScale)
  }

  context.strokeStyle = 'rgba(37, 99, 235, 0.34)'
  context.lineWidth = 0.8 * inverseScale
  context.beginPath()
  context.moveTo(-30 * inverseScale, labelY - 14 * inverseScale)
  context.lineTo(-12 * inverseScale, labelY - 14 * inverseScale)
  context.moveTo(12 * inverseScale, labelY - 14 * inverseScale)
  context.lineTo(30 * inverseScale, labelY - 14 * inverseScale)
  context.stroke()
  context.restore()
}

function drawSituationField(frame: MapFrame, evidenceBitmap: HTMLCanvasElement | null) {
  if (!context || !Number.isFinite(frame.resolution) || frame.resolution <= 0) return
  const fieldWidth = frame.width * frame.resolution
  const fieldHeight = frame.height * frame.resolution
  if (!Number.isFinite(fieldWidth) || !Number.isFinite(fieldHeight) || fieldWidth <= 0 || fieldHeight <= 0) return

  const inverseScale = 1 / Math.max(view.scale, 1)
  const minorSpacing = view.scale >= 108 ? 0.5 : 1
  const majorSpacing = 2
  const cornerLength = Math.min(
    Math.max(12 * inverseScale, 0.16),
    Math.max(0.12, Math.min(fieldWidth, fieldHeight) * 0.1),
  )

  context.save()
  context.translate(frame.originX, frame.originY)
  context.rotate(frame.originYaw)

  context.fillStyle = '#f8fbf8'
  context.fillRect(0, 0, fieldWidth, fieldHeight)

  if (evidenceBitmap) {
    context.save()
    context.globalAlpha = 0.92
    context.imageSmoothingEnabled = false
    context.scale(frame.resolution, frame.resolution)
    context.drawImage(evidenceBitmap, 0, 0, frame.width, frame.height)
    context.restore()
  }

  context.save()
  context.beginPath()
  context.rect(0, 0, fieldWidth, fieldHeight)
  context.clip()

  if (view.scale >= 48) {
    context.beginPath()
    context.strokeStyle = 'rgba(64, 86, 82, 0.052)'
    context.lineWidth = 0.75 * inverseScale
    for (let x = minorSpacing; x < fieldWidth; x += minorSpacing) {
      context.moveTo(x, 0)
      context.lineTo(x, fieldHeight)
    }
    for (let y = minorSpacing; y < fieldHeight; y += minorSpacing) {
      context.moveTo(0, y)
      context.lineTo(fieldWidth, y)
    }
    context.stroke()
  }

  context.beginPath()
  context.strokeStyle = 'rgba(50, 79, 75, 0.11)'
  context.lineWidth = 0.9 * inverseScale
  for (let x = majorSpacing; x < fieldWidth; x += majorSpacing) {
    context.moveTo(x, 0)
    context.lineTo(x, fieldHeight)
  }
  for (let y = majorSpacing; y < fieldHeight; y += majorSpacing) {
    context.moveTo(0, y)
    context.lineTo(fieldWidth, y)
  }
  context.stroke()
  context.restore()

  drawSituationNoEvidence(fieldWidth, fieldHeight, inverseScale, frame.originYaw)

  context.strokeStyle = 'rgba(48, 78, 75, 0.28)'
  context.lineWidth = 0.8 * inverseScale
  context.setLineDash([4 * inverseScale, 4 * inverseScale])
  context.strokeRect(0, 0, fieldWidth, fieldHeight)
  context.setLineDash([])

  context.strokeStyle = 'rgba(37, 99, 235, 0.54)'
  context.lineWidth = 1.3 * inverseScale
  context.lineCap = 'square'
  context.beginPath()
  context.moveTo(0, cornerLength)
  context.lineTo(0, 0)
  context.lineTo(cornerLength, 0)
  context.moveTo(fieldWidth - cornerLength, 0)
  context.lineTo(fieldWidth, 0)
  context.lineTo(fieldWidth, cornerLength)
  context.moveTo(fieldWidth, fieldHeight - cornerLength)
  context.lineTo(fieldWidth, fieldHeight)
  context.lineTo(fieldWidth - cornerLength, fieldHeight)
  context.moveTo(cornerLength, fieldHeight)
  context.lineTo(0, fieldHeight)
  context.lineTo(0, fieldHeight - cornerLength)
  context.stroke()
  context.restore()
}

function drawLayer(key: LayerKey, drawLayerContent: () => void) {
  if (!context) return
  context.save()
  context.globalAlpha = layerOpacity(key)
  drawLayerContent()
  context.restore()
}

/* 只补充读图纹理，不参与任何占据格或导航判断。 */
function drawTerrainGrid() {
  if (!context || !mapBounds || view.scale < 56) return
  const spacing = view.scale >= 155 ? 0.25 : view.scale >= 96 ? 0.5 : 1
  const halfWidth = logicalWidth / (view.scale * 2)
  const halfHeight = logicalHeight / (view.scale * 2)
  const minX = Math.max(mapBounds.minX, view.centerX - halfWidth)
  const maxX = Math.min(mapBounds.maxX, view.centerX + halfWidth)
  const minY = Math.max(mapBounds.minY, view.centerY - halfHeight)
  const maxY = Math.min(mapBounds.maxY, view.centerY + halfHeight)
  const columns = Math.ceil((maxX - minX) / spacing)
  const rows = Math.ceil((maxY - minY) / spacing)
  if (columns <= 0 || rows <= 0 || columns + rows > 140) return

  context.save()
  context.beginPath()
  context.strokeStyle = 'rgba(67, 84, 88, 0.12)'
  context.lineWidth = 1 / view.scale
  const startX = Math.ceil(minX / spacing) * spacing
  const startY = Math.ceil(minY / spacing) * spacing
  for (let x = startX; x <= maxX; x += spacing) {
    context.moveTo(x, minY)
    context.lineTo(x, maxY)
  }
  for (let y = startY; y <= maxY; y += spacing) {
    context.moveTo(minX, y)
    context.lineTo(maxX, y)
  }
  context.stroke()
  context.restore()
}

function drawPairs(
  values: Float32Array | null,
  {
    color,
    width,
    pointRadius = 0,
    maxPoints = MAX_PATH_RENDER_POINTS,
    haloColor,
    haloWidth = 0,
  }: PairStrokeStyle,
) {
  if (!context || !values || values.length < 2) return
  context.save()
  context.strokeStyle = color
  context.fillStyle = color
  context.lineWidth = width / Math.max(view.scale, 1)
  context.lineJoin = 'round'
  context.lineCap = 'round'
  const sourceCount = Math.floor(values.length / 2)
  const stride = Math.max(1, Math.ceil(sourceCount / Math.max(maxPoints, 1)))
  context.beginPath()
  let moved = false
  let hasPath = false
  let segmentPointIndex = 0
  for (let index = 0; index + 1 < values.length; index += 2) {
    const x = values[index]!
    const y = values[index + 1]!
    if (!Number.isFinite(x) || !Number.isFinite(y)) {
      moved = false
      segmentPointIndex = 0
      continue
    }
    const nextX = values[index + 2]
    const nextY = values[index + 3]
    const isSegmentEnd = !Number.isFinite(nextX) || !Number.isFinite(nextY)
    if (!moved) {
      context.moveTo(x, y)
      moved = true
      hasPath = true
    } else if (segmentPointIndex % stride === 0 || isSegmentEnd) {
      context.lineTo(x, y)
    }
    segmentPointIndex += 1
  }
  if (hasPath && haloColor && haloWidth > width) {
    context.strokeStyle = haloColor
    context.lineWidth = haloWidth / Math.max(view.scale, 1)
    context.stroke()
  }
  if (hasPath) {
    context.strokeStyle = color
    context.lineWidth = width / Math.max(view.scale, 1)
    context.stroke()
  }
  if (pointRadius > 0) {
    const radius = pointRadius / Math.max(view.scale, 1)
    segmentPointIndex = 0
    let hasPoint = false
    context.beginPath()
    for (let index = 0; index + 1 < values.length; index += 2) {
      const x = values[index]!
      const y = values[index + 1]!
      if (!Number.isFinite(x) || !Number.isFinite(y)) {
        segmentPointIndex = 0
        continue
      }
      const nextX = values[index + 2]
      const nextY = values[index + 3]
      const isSegmentEnd = !Number.isFinite(nextX) || !Number.isFinite(nextY)
      if (segmentPointIndex % stride !== 0 && !isSegmentEnd) {
        segmentPointIndex += 1
        continue
      }
      context.arc(x, y, radius, 0, Math.PI * 2)
      hasPoint = true
      segmentPointIndex += 1
    }
    if (hasPoint) context.fill()
  }
  context.restore()
}

function drawPoints(
  values: Float32Array | null,
  color: string,
  pointRadius: number,
  maxPoints = MAX_SCAN_RENDER_POINTS,
) {
  if (!context || !values || values.length < 2) return
  context.save()
  context.fillStyle = color
  const radius = pointRadius / Math.max(view.scale, 1)
  const sourceCount = Math.floor(values.length / 2)
  const stride = Math.max(1, Math.ceil(sourceCount / Math.max(maxPoints, 1)))
  context.beginPath()
  let hasPoint = false
  for (let index = 0; index + 1 < values.length; index += stride * 2) {
    const x = values[index]!
    const y = values[index + 1]!
    if (!Number.isFinite(x) || !Number.isFinite(y)) continue
    context.moveTo(x + radius, y)
    context.arc(x, y, radius, 0, Math.PI * 2)
    hasPoint = true
  }
  if (hasPoint) context.fill()
  context.restore()
}

function drawParticles(values: Float32Array | null) {
  drawPoints(values, 'rgba(37, 99, 235, 0.42)', 2.2, MAX_PARTICLE_RENDER_POINTS)
}

function drawTrajectory() {
  if (!context || trajectory.length < 2) return
  context.save()
  context.strokeStyle = 'rgba(58, 88, 84, 0.48)'
  context.lineWidth = 1.25 / Math.max(view.scale, 1)
  context.lineJoin = 'round'
  context.lineCap = 'round'
  context.setLineDash([3.5 / Math.max(view.scale, 1), 4.5 / Math.max(view.scale, 1)])
  context.beginPath()
  trajectory.forEach((point, index) => {
    if (index === 0) context?.moveTo(point.x, point.y)
    else context?.lineTo(point.x, point.y)
  })
  context.stroke()
  context.restore()
}

function drawTrackedGoal(goal: NavigationPose | null, pose: RobotPose | null) {
  if (!context || !goal) return
  const inverseScale = 1 / Math.max(view.scale, 1)
  context.save()
  if (pose && [pose.x, pose.y].every(Number.isFinite)) {
    context.strokeStyle = 'rgba(217, 119, 6, 0.58)'
    context.lineWidth = 1.2 * inverseScale
    context.setLineDash([5 * inverseScale, 4 * inverseScale])
    context.beginPath()
    context.moveTo(pose.x, pose.y)
    context.lineTo(goal.x, goal.y)
    context.stroke()
    context.setLineDash([])
  }

  context.translate(goal.x, goal.y)
  context.rotate(goal.theta)
  context.strokeStyle = '#d97706'
  context.fillStyle = '#d97706'
  context.lineWidth = 1.8 * inverseScale
  context.beginPath()
  context.arc(0, 0, 11 * inverseScale, 0, Math.PI * 2)
  context.stroke()
  context.beginPath()
  context.moveTo(0, 0)
  context.lineTo(30 * inverseScale, 0)
  context.lineTo(24 * inverseScale, 5 * inverseScale)
  context.moveTo(30 * inverseScale, 0)
  context.lineTo(24 * inverseScale, -5 * inverseScale)
  context.stroke()
  context.beginPath()
  context.arc(0, 0, 2.8 * inverseScale, 0, Math.PI * 2)
  context.fill()
  context.restore()
}

function drawSelectionBox(box: SelectionBox | null) {
  if (!context || !box) return
  const minX = Math.min(box.start.x, box.end.x)
  const minY = Math.min(box.start.y, box.end.y)
  const width = Math.abs(box.end.x - box.start.x)
  const height = Math.abs(box.end.y - box.start.y)
  if (width <= 0 || height <= 0) return

  const inverseScale = 1 / Math.max(view.scale, 1)
  context.save()
  context.fillStyle = 'rgba(37, 99, 235, 0.08)'
  context.strokeStyle = 'rgba(37, 99, 235, 0.82)'
  context.lineWidth = 1.1 * inverseScale
  context.setLineDash([5 * inverseScale, 3 * inverseScale])
  context.fillRect(minX, minY, width, height)
  context.strokeRect(minX, minY, width, height)
  context.restore()
}

function drawRobot(pose: RobotPose | null) {
  if (!context || !pose || ![pose.x, pose.y, pose.theta].every(Number.isFinite)) return
  const inverseScale = 1 / Math.max(view.scale, 1)
  const robotRadius = Math.max(0.22, 11 * inverseScale)
  const headingStart = Math.max(0.16, 7 * inverseScale)
  const headingEnd = headingStart + 12 * inverseScale
  context.save()
  context.translate(pose.x, pose.y)
  context.rotate(pose.theta)
  context.strokeStyle = 'rgba(37, 99, 235, 0.32)'
  context.lineWidth = 0.9 * inverseScale
  context.beginPath()
  context.arc(0, 0, robotRadius, 0, Math.PI * 2)
  context.stroke()
  context.strokeStyle = 'rgba(37, 99, 235, 0.72)'
  context.lineWidth = 0.8 * inverseScale
  context.beginPath()
  context.moveTo(headingStart, 0)
  context.lineTo(headingEnd, 0)
  context.stroke()
  context.strokeStyle = '#2563eb'
  context.fillStyle = 'rgba(37, 99, 235, 0.2)'
  context.lineWidth = 1.5 / Math.max(view.scale, 1)
  context.beginPath()
  context.moveTo(0.24, 0)
  context.lineTo(-0.16, 0.14)
  context.lineTo(-0.16, -0.14)
  context.closePath()
  context.fill()
  context.stroke()
  context.beginPath()
  context.arc(0, 0, 0.08, 0, Math.PI * 2)
  context.fillStyle = '#2563eb'
  context.fill()
  context.restore()
}

function drawRobotSelection(pose: RobotPose | null) {
  if (!context || !robotSelected.value || !pose || ![pose.x, pose.y].every(Number.isFinite)) return
  const inverseScale = 1 / Math.max(view.scale, 1)
  const half = 17 * inverseScale
  const corner = 7 * inverseScale
  context.save()
  context.translate(pose.x, pose.y)
  context.strokeStyle = '#2563eb'
  context.lineWidth = 1.4 * inverseScale
  context.lineCap = 'square'
  context.beginPath()
  context.moveTo(-half, -half + corner)
  context.lineTo(-half, -half)
  context.lineTo(-half + corner, -half)
  context.moveTo(half - corner, -half)
  context.lineTo(half, -half)
  context.lineTo(half, -half + corner)
  context.moveTo(half, half - corner)
  context.lineTo(half, half)
  context.lineTo(half - corner, half)
  context.moveTo(-half + corner, half)
  context.lineTo(-half, half)
  context.lineTo(-half, half - corner)
  context.stroke()
  if (Number.isFinite(pose.theta)) {
    const heading = half + 5 * inverseScale
    context.save()
    context.rotate(pose.theta)
    context.globalAlpha = 0.82
    context.lineWidth = 1.15 * inverseScale
    context.beginPath()
    context.moveTo(heading, -4 * inverseScale)
    context.lineTo(heading + 7 * inverseScale, 0)
    context.lineTo(heading, 4 * inverseScale)
    context.stroke()
    context.restore()
  }
  context.restore()
}

function drawDraftPose(pose: NavigationPose | null) {
  if (!context || !pose || interactionMode.value === 'browse') return
  const color = interactionMode.value === 'goal' ? '#d97706' : '#0f766e'
  const inverseScale = 1 / Math.max(view.scale, 1)
  const ringRadius = 11 * inverseScale
  const arrowLength = 34 * inverseScale
  const arrowWidth = 6 * inverseScale

  context.save()
  context.translate(pose.x, pose.y)
  context.rotate(pose.theta)
  context.strokeStyle = color
  context.fillStyle = color
  context.lineWidth = 1.6 * inverseScale
  context.setLineDash([4 * inverseScale, 3 * inverseScale])
  context.globalAlpha = 0.86
  context.beginPath()
  context.arc(0, 0, ringRadius, 0, Math.PI * 2)
  context.stroke()
  context.setLineDash([])
  context.globalAlpha = 1
  if (interactionMode.value === 'initialPose') {
    context.beginPath()
    context.moveTo(0, -ringRadius - 5 * inverseScale)
    context.lineTo(0, -ringRadius + 1 * inverseScale)
    context.moveTo(ringRadius + 5 * inverseScale, 0)
    context.lineTo(ringRadius - 1 * inverseScale, 0)
    context.moveTo(0, ringRadius + 5 * inverseScale)
    context.lineTo(0, ringRadius - 1 * inverseScale)
    context.moveTo(-ringRadius - 5 * inverseScale, 0)
    context.lineTo(-ringRadius + 1 * inverseScale, 0)
    context.stroke()
  }
  context.beginPath()
  context.moveTo(0, 0)
  context.lineTo(arrowLength, 0)
  context.lineTo(arrowLength - arrowWidth, arrowWidth)
  context.moveTo(arrowLength, 0)
  context.lineTo(arrowLength - arrowWidth, -arrowWidth)
  context.stroke()
  context.beginPath()
  context.arc(0, 0, 2.8 * inverseScale, 0, Math.PI * 2)
  context.fill()
  context.restore()
}

function drawLandingEffects() {
  if (!context || reduceMotion || landingEffects.length === 0) return
  const now = performance.now()
  const inverseScale = 1 / Math.max(view.scale, 1)
  for (let index = landingEffects.length - 1; index >= 0; index -= 1) {
    const effect = landingEffects[index]!
    const duration = effect.kind === 'select'
      ? 160
      : effect.kind === 'goal'
        ? 180
        : effect.kind === 'blocked'
          ? 120
          : 220
    const progress = (now - effect.startedAt) / duration
    if (progress >= 1) {
      landingEffects.splice(index, 1)
      continue
    }
    const color = effect.kind === 'goal'
      ? '#d97706'
      : effect.kind === 'initialPose'
        ? '#0f766e'
        : effect.kind === 'blocked'
          ? '#dc2626'
          : '#2563eb'
    context.save()
    context.translate(effect.x, effect.y)
    context.strokeStyle = color
    context.lineWidth = 1.25 * inverseScale
    context.globalAlpha = 0.82 * (1 - progress)
    if (effect.kind === 'goal') {
      const radius = (7 + progress * 15) * inverseScale
      context.beginPath()
      for (let segment = 0; segment < 3; segment += 1) {
        const start = segment * (Math.PI * 2 / 3) + progress * 0.22
        context.arc(0, 0, radius, start, start + 0.62)
      }
      context.stroke()
    } else if (effect.kind === 'initialPose') {
      context.beginPath()
      context.arc(0, 0, (5 + progress * 8) * inverseScale, 0, Math.PI * 2)
      context.stroke()
      context.globalAlpha *= 0.72
      context.beginPath()
      context.arc(0, 0, (10 + progress * 14) * inverseScale, 0, Math.PI * 2)
      context.stroke()
    } else if (effect.kind === 'blocked') {
      const radius = (12 - progress * 6) * inverseScale
      context.beginPath()
      context.moveTo(-radius, -radius)
      context.lineTo(radius, radius)
      context.moveTo(-radius, radius)
      context.lineTo(radius, -radius)
      context.stroke()
    } else {
      context.beginPath()
      context.arc(0, 0, (6 + progress * 13) * inverseScale, 0, Math.PI * 2)
      context.stroke()
    }
    context.restore()
  }
  if (landingEffects.length > 0) scheduleDraw()
}

function draw() {
  if (!context || !canvasEl.value) return
  const width = logicalWidth
  const height = logicalHeight
  context.setTransform(devicePixelRatioValue, 0, 0, devicePixelRatioValue, 0, 0)
  context.clearRect(0, 0, width, height)
  context.fillStyle = '#edf1f4'
  context.fillRect(0, 0, width, height)
  if (!props.map) return

  setWorldTransform()
  const drawPresentation = situationFieldEnabled.value && mapViewPreset.value !== 'debug'
  if (drawPresentation) {
    drawSituationField(props.map, situationEvidenceBitmap)
  } else {
    /* 解析视图仍依赖原始栅格位图；态势视图不应被该异步缓存挡住。 */
    if (!mapBitmap) return
    drawRaster(
      mapBitmap,
      props.map.originX,
      props.map.originY,
      props.map.resolution,
      props.map.width,
      props.map.height,
      props.map.originYaw,
    )
    drawTerrainGrid()
  }
  if (layers.value.globalCostmap && props.globalCostmap) {
    drawLayer('globalCostmap', () => drawRaster(
      globalCostmapBitmap,
      props.globalCostmap!.originX,
      props.globalCostmap!.originY,
      props.globalCostmap!.resolution,
      props.globalCostmap!.width,
      props.globalCostmap!.height,
    ))
  }
  if (layers.value.localCostmap && props.localCostmap) {
    drawLayer('localCostmap', () => drawRaster(
      localCostmapBitmap,
      props.localCostmap!.originX,
      props.localCostmap!.originY,
      props.localCostmap!.resolution,
      props.localCostmap!.width,
      props.localCostmap!.height,
    ))
  }
  if (layers.value.mapPatch && props.mapPatch) {
    drawLayer('mapPatch', () => drawRaster(
      mapPatchBitmap,
      props.mapPatch!.originX,
      props.mapPatch!.originY,
      props.mapPatch!.resolution,
      props.mapPatch!.width,
      props.mapPatch!.height,
    ))
  }
  if (layers.value.path) {
    drawLayer('path', () => drawPairs(props.path, {
      color: '#2563eb',
      width: 1.9,
      maxPoints: MAX_PATH_RENDER_POINTS,
      haloColor: 'rgba(37, 99, 235, 0.17)',
      haloWidth: 5.6,
    }))
  }
  if (layers.value.localPlan) {
    drawLayer('localPlan', () => drawPairs(props.localPlan, {
      color: '#0f766e',
      width: 1.65,
      pointRadius: 1.35,
      maxPoints: MAX_LOCAL_PLAN_RENDER_POINTS,
      haloColor: 'rgba(13, 148, 136, 0.16)',
      haloWidth: 4.8,
    }))
  }
  if (layers.value.scan) drawLayer('scan', () => drawPoints(props.scan, '#dc2626', 2.2, MAX_SCAN_RENDER_POINTS))
  if (layers.value.particles) drawLayer('particles', () => drawParticles(props.particles))
  if (layers.value.trajectory) drawTrajectory()
  drawSelectionBox(selectionBox.value)
  drawTrackedGoal(trackedGoal.value, props.robotPose)
  drawRobot(props.robotPose)
  drawRobotSelection(props.robotPose)
  drawDraftPose(draftPose.value)
  drawLandingEffects()
}

function resize() {
  if (!canvasEl.value || !hostEl.value || !context) return
  logicalWidth = Math.max(1, hostEl.value.clientWidth)
  logicalHeight = Math.max(1, hostEl.value.clientHeight)
  devicePixelRatioValue = Math.min(window.devicePixelRatio || 1, 2)
  canvasEl.value.width = Math.round(logicalWidth * devicePixelRatioValue)
  canvasEl.value.height = Math.round(logicalHeight * devicePixelRatioValue)
  if (props.map && !fittedMapGeometrySignature) fitView()
  scheduleDraw()
}

function clientToCanvas(clientX: number, clientY: number): ScreenPoint {
  const rect = canvasEl.value?.getBoundingClientRect()
  if (!rect || rect.width <= 0 || rect.height <= 0) {
    return { x: logicalWidth / 2, y: logicalHeight / 2 }
  }
  return {
    x: (clientX - rect.left) * logicalWidth / rect.width,
    y: (clientY - rect.top) * logicalHeight / rect.height,
  }
}

function canvasToWorld(point: ScreenPoint): WorldPoint {
  return {
    x: view.centerX + (point.x - logicalWidth / 2) / view.scale,
    y: view.centerY - (point.y - logicalHeight / 2) / view.scale,
  }
}

function screenToWorld(clientX: number, clientY: number) {
  return canvasToWorld(clientToCanvas(clientX, clientY))
}

function isPointInMap(point: WorldPoint) {
  const frame = props.map
  if (!frame || !Number.isFinite(frame.resolution) || frame.resolution <= 0) return false
  const dx = point.x - frame.originX
  const dy = point.y - frame.originY
  const cosine = Math.cos(frame.originYaw)
  const sine = Math.sin(frame.originYaw)
  const localX = cosine * dx + sine * dy
  const localY = -sine * dx + cosine * dy
  const width = frame.width * frame.resolution
  const height = frame.height * frame.resolution
  const epsilon = frame.resolution * 0.01
  return localX >= -epsilon && localY >= -epsilon && localX <= width + epsilon && localY <= height + epsilon
}

function mapCellState(point: WorldPoint): MapCellState {
  const frame = props.map
  if (!frame || !Number.isFinite(frame.resolution) || frame.resolution <= 0) return 'unavailable'

  const dx = point.x - frame.originX
  const dy = point.y - frame.originY
  const cosine = Math.cos(frame.originYaw)
  const sine = Math.sin(frame.originYaw)
  const localX = cosine * dx + sine * dy
  const localY = -sine * dx + cosine * dy
  const cellX = Math.floor(localX / frame.resolution)
  const cellY = Math.floor(localY / frame.resolution)
  if (cellX < 0 || cellY < 0 || cellX >= frame.width || cellY >= frame.height) return 'outside'

  const value = Number(frame.data[cellY * frame.width + cellX])
  if (!Number.isFinite(value) || value < 0) return 'unknown'
  return value >= OCCUPIED_CELL_THRESHOLD ? 'occupied' : 'free'
}

function goalBlockReason(point: WorldPoint) {
  if (mapCellState(point) === 'occupied') return '目标落在当前底图的占据栅格内。'
  return ''
}

function clearLongPressTimer() {
  clearTimeout(longPressTimer)
  longPressTimer = undefined
}

function addLandingEffect(point: WorldPoint, kind: LandingKind) {
  if (!reduceMotion) {
    landingEffects.push({ ...point, kind, startedAt: performance.now() })
    if (landingEffects.length > 4) landingEffects.splice(0, landingEffects.length - 4)
  }
  scheduleDraw()
}

function defaultPoseTheta(point: WorldPoint, mode: PlacementMode) {
  const pose = props.robotPose
  if (mode === 'goal' && pose && [pose.x, pose.y].every(Number.isFinite)) {
    const dx = point.x - pose.x
    const dy = point.y - pose.y
    if (Math.hypot(dx, dy) > 0.001) return Math.atan2(dy, dx)
  }
  return Number.isFinite(pose?.theta) ? pose!.theta : 0
}

function isRobotHit(point: WorldPoint) {
  const pose = props.robotPose
  if (!pose || ![pose.x, pose.y].every(Number.isFinite)) return false
  return Math.hypot(point.x - pose.x, point.y - pose.y) * view.scale <= ROBOT_HIT_RADIUS_PX
}

function isRobotWithinBox(box: SelectionBox) {
  const pose = props.robotPose
  if (!pose || ![pose.x, pose.y].every(Number.isFinite)) return false
  return (
    pose.x >= Math.min(box.start.x, box.end.x)
    && pose.x <= Math.max(box.start.x, box.end.x)
    && pose.y >= Math.min(box.start.y, box.end.y)
    && pose.y <= Math.max(box.start.y, box.end.y)
  )
}

function selectRobot(selected: boolean, point?: WorldPoint) {
  robotSelected.value = selected
  if (selected && point) {
    const pose = props.robotPose
    addLandingEffect(
      pose && [pose.x, pose.y].every(Number.isFinite) ? { x: pose.x, y: pose.y } : point,
      'select',
    )
  }
  scheduleDraw()
}

function showBlockedInteraction(point: WorldPoint, message: string) {
  submitState.value = 'error'
  submitMessage.value = message
  addLandingEffect(point, 'blocked')
}

function resetSubmission() {
  submitState.value = 'idle'
  submitMessage.value = ''
}

function startGoalTracking(goal: NavigationPose) {
  trackedGoal.value = { ...goal }
  trackedGoalStartedAt.value = Date.now()
  trackedGoalLastProgressAt.value = trackedGoalStartedAt.value
  trackedGoalClosestDistance.value = props.robotPose
    ? Math.hypot(goal.x - props.robotPose.x, goal.y - props.robotPose.y)
    : Infinity
  trackedGoalMotionDetected.value = false
  trackedGoalStartPose.value = props.robotPose ? { ...props.robotPose } : null
  scheduleDraw()
}

function stopGoalTracking() {
  trackedGoal.value = null
  trackedGoalStartedAt.value = 0
  trackedGoalLastProgressAt.value = 0
  trackedGoalClosestDistance.value = Infinity
  trackedGoalMotionDetected.value = false
  trackedGoalStartPose.value = null
  scheduleDraw()
}

function setInteractionMode(next: InteractionMode) {
  if (next === 'goal' && localizationState.value !== 'ready') {
    submitState.value = 'error'
    submitMessage.value = navigationBlockReason.value || '请先确认初始位姿'
    return
  }
  if (next === 'initialPose') localizationState.value = 'needsInitialPose'
  interactionMode.value = next
  clearLongPressTimer()
  pointerAction = null
  selectionBox.value = null
  draftPose.value = null
  queuedGoalDraft.value = false
  resetSubmission()
  scheduleDraw()
}

function cancelDraft() {
  clearLongPressTimer()
  pointerAction = null
  selectionBox.value = null
  draftPose.value = null
  queuedGoalDraft.value = false
  if (interactionMode.value !== 'browse') interactionMode.value = 'browse'
  resetSubmission()
  scheduleDraw()
}

function beginInitialPose() {
  if (!props.map) {
    submitState.value = 'error'
    submitMessage.value = '等待地图帧'
    return
  }
  localizationState.value = 'needsInitialPose'
  setInteractionMode('initialPose')
}

function confirmLocalization() {
  if (localizationState.value !== 'awaitingConfirmation') return
  localizationState.value = 'ready'
  interactionMode.value = 'browse'
  draftPose.value = null
  queuedGoalDraft.value = false
  resetSubmission()
  submitMessage.value = '定位已由操作者确认，导航已解锁。'
  if (props.robotPose && [props.robotPose.x, props.robotPose.y].every(Number.isFinite)) {
    selectRobot(true, props.robotPose)
  }
  scheduleDraw()
}

function beginPoseDraft(event: PointerEvent, mode: PlacementMode, autoSubmit: boolean) {
  const point = screenToWorld(event.clientX, event.clientY)
  if (!isPointInMap(point)) {
    showBlockedInteraction(point, '落点不在当前地图范围内。')
    return false
  }
  const occupancyReason = mode === 'goal' ? goalBlockReason(point) : ''
  if (occupancyReason) {
    showBlockedInteraction(point, occupancyReason)
    return false
  }
  if (submitState.value === 'submitting') {
    showBlockedInteraction(point, '上一条地图指令正在提交。')
    return false
  }

  resetSubmission()
  const reason = mode === 'goal' ? navigationBlockReason.value : initialPoseBlockReason.value
  if (reason) {
    showBlockedInteraction(point, reason)
    return false
  }

  interactionMode.value = mode
  queuedGoalDraft.value = false
  draftPose.value = { x: point.x, y: point.y, theta: defaultPoseTheta(point, mode) }
  pointerAction = {
    kind: 'pose',
    pointerId: event.pointerId,
    clientX: event.clientX,
    clientY: event.clientY,
    x: point.x,
    y: point.y,
    mode,
    autoSubmit,
  }
  canvasEl.value?.setPointerCapture(event.pointerId)
  scheduleDraw()
  return true
}

function startBrowsePointer(event: PointerEvent) {
  const point = screenToWorld(event.clientX, event.clientY)
  canvasEl.value?.setPointerCapture(event.pointerId)
  pointerAction = {
    kind: 'browse',
    pointerId: event.pointerId,
    clientX: event.clientX,
    clientY: event.clientY,
    startClientX: event.clientX,
    startClientY: event.clientY,
    startWorld: point,
    longPress: false,
  }
  clearLongPressTimer()
  longPressTimer = setTimeout(() => {
    if (pointerAction?.kind === 'browse' && pointerAction.pointerId === event.pointerId) {
      pointerAction.longPress = true
    }
  }, LONG_PRESS_DELAY_MS)
}

function startPanPointer(event: PointerEvent) {
  clearLongPressTimer()
  canvasEl.value?.setPointerCapture(event.pointerId)
  pointerAction = { kind: 'pan', pointerId: event.pointerId, x: event.clientX, y: event.clientY }
  followRobot.value = false
}

function onPointerDown(event: PointerEvent) {
  if (!props.map) return
  if (event.button === 2) {
    event.preventDefault()
    if (interactionMode.value === 'initialPose') return
    const began = beginPoseDraft(event, 'goal', !event.shiftKey)
    if (began) {
      if (props.robotPose && [props.robotPose.x, props.robotPose.y].every(Number.isFinite)) {
        selectRobot(true, props.robotPose)
      }
      if (event.shiftKey) {
        queuedGoalDraft.value = true
        submitMessage.value = '队列接口尚未提供；本次仅保留本地预览。'
      }
    }
    return
  }
  if (event.button === 1) {
    event.preventDefault()
    startPanPointer(event)
    return
  }
  if (event.button !== 0) return

  if (interactionMode.value === 'browse') {
    startBrowsePointer(event)
    return
  }

  beginPoseDraft(event, interactionMode.value, false)
}

function onPointerMove(event: PointerEvent) {
  const action = pointerAction
  if (!action || action.pointerId !== event.pointerId) {
    scheduleDraw()
    return
  }
  if (action.kind === 'browse') {
    const distance = Math.hypot(event.clientX - action.startClientX, event.clientY - action.startClientY)
    if (distance < POINTER_DRAG_THRESHOLD) return
    clearLongPressTimer()
    if (!action.longPress) return
    const end = screenToWorld(event.clientX, event.clientY)
    pointerAction = { kind: 'box', pointerId: event.pointerId, start: action.startWorld, end }
    selectionBox.value = { start: action.startWorld, end }
    scheduleDraw()
    return
  }
  if (action.kind === 'pan') {
    const dx = event.clientX - action.x
    const dy = event.clientY - action.y
    if (dx !== 0 || dy !== 0) followRobot.value = false
    view.centerX -= dx / view.scale
    view.centerY += dy / view.scale
    action.x = event.clientX
    action.y = event.clientY
    scheduleDraw()
    return
  }
  if (action.kind === 'box') {
    const end = screenToWorld(event.clientX, event.clientY)
    action.end = end
    selectionBox.value = { start: action.start, end }
    scheduleDraw()
    return
  }

  const distance = Math.hypot(
    event.clientX - action.clientX,
    event.clientY - action.clientY,
  )
  if (distance >= POINTER_DRAG_THRESHOLD) {
    const direction = screenToWorld(event.clientX, event.clientY)
    draftPose.value = {
      x: action.x,
      y: action.y,
      theta: Math.atan2(direction.y - action.y, direction.x - action.x),
    }
  }
  scheduleDraw()
}

function onPointerUp(event: PointerEvent) {
  const action = pointerAction
  clearLongPressTimer()
  if (!action || action.pointerId !== event.pointerId) return
  if (action.kind === 'pose') {
    onPointerMove(event)
  } else if (action.kind === 'browse') {
    const distance = Math.hypot(event.clientX - action.startClientX, event.clientY - action.startClientY)
    if (distance < POINTER_DRAG_THRESHOLD) selectRobot(isRobotHit(action.startWorld), action.startWorld)
  } else if (action.kind === 'box') {
    const end = screenToWorld(event.clientX, event.clientY)
    const box = { start: action.start, end }
    selectionBox.value = null
    selectRobot(isRobotWithinBox(box), end)
  }
  if (canvasEl.value?.hasPointerCapture(event.pointerId)) canvasEl.value.releasePointerCapture(event.pointerId)
  pointerAction = null
  if (action.kind === 'pose' && action.mode === 'goal' && action.autoSubmit && !queuedGoalDraft.value) {
    void submitDraft({ restoreBrowseAfterGoal: true })
  }
  scheduleDraw()
}

function onPointerCancel(event: PointerEvent) {
  const action = pointerAction
  clearLongPressTimer()
  if (canvasEl.value?.hasPointerCapture(event.pointerId)) canvasEl.value.releasePointerCapture(event.pointerId)
  if (action?.pointerId === event.pointerId && action.kind === 'pose') cancelDraft()
  if (action?.pointerId === event.pointerId && action.kind === 'box') selectionBox.value = null
  pointerAction = null
  scheduleDraw()
}

function onContextMenu(event: MouseEvent) {
  event.preventDefault()
}

function onMapKeyDown(event: KeyboardEvent) {
  if (event.key !== 'Escape') return
  if (!pointerAction && !draftPose.value && interactionMode.value === 'browse' && !selectionBox.value) return
  /* 仅拦截本地预览的 Esc，已提交的 Nav2 目标仍由后端任务系统负责。 */
  event.preventDefault()
  event.stopImmediatePropagation()
  cancelDraft()
}

function onDoubleClick() {
  if (interactionMode.value === 'browse') fitMap()
}

function onWheel(event: WheelEvent) {
  followRobot.value = false
  const before = screenToWorld(event.clientX, event.clientY)
  const factor = event.deltaY < 0 ? 1.16 : 1 / 1.16
  view.scale = Math.min(900, Math.max(2, view.scale * factor))
  const after = screenToWorld(event.clientX, event.clientY)
  view.centerX += before.x - after.x
  view.centerY += before.y - after.y
  scheduleDraw()
}

function describeNavigationError(error: unknown) {
  const message = error instanceof Error ? error.message : '地图指令提交失败。'
  const hubException = message.match(/HubException:\s*(.+)$/i)
  return hubException ? `HubException: ${hubException[1]}` : message
}

function angularDistance(a: number, b: number) {
  return Math.abs(Math.atan2(Math.sin(a - b), Math.cos(a - b)))
}

function isDuplicateGoal(pose: NavigationPose) {
  if (!lastSubmittedGoal || Date.now() - lastGoalSubmittedAt > GOAL_DEDUP_WINDOW_MS) return false
  return (
    Math.hypot(pose.x - lastSubmittedGoal.x, pose.y - lastSubmittedGoal.y) <= 0.08
    && angularDistance(pose.theta, lastSubmittedGoal.theta) <= 0.12
  )
}

async function submitDraft(options: { restoreBrowseAfterGoal?: boolean } = {}) {
  const pose = draftPose.value
  const mode = interactionMode.value
  if (!pose || mode === 'browse' || submitState.value === 'submitting') return
  if (!isPointInMap(pose)) {
    submitState.value = 'error'
    submitMessage.value = '落点不在当前地图范围内。'
    return
  }
  const occupancyReason = mode === 'goal' ? goalBlockReason(pose) : ''
  if (occupancyReason) {
    submitState.value = 'error'
    submitMessage.value = occupancyReason
    return
  }
  const reason = mode === 'goal' ? navigationBlockReason.value : initialPoseBlockReason.value
  if (reason) {
    submitState.value = 'error'
    submitMessage.value = reason
    return
  }

  const action = mode === 'goal' ? props.setGoal : props.setInitialPose
  if (!action) {
    submitState.value = 'error'
    submitMessage.value = '地图指令入口未就绪。'
    return
  }

  if (mode === 'goal' && isDuplicateGoal(pose)) {
    submitState.value = 'sent'
    submitMessage.value = '重复目标已忽略。'
    draftPose.value = null
    queuedGoalDraft.value = false
    if (options.restoreBrowseAfterGoal) interactionMode.value = 'browse'
    scheduleDraw()
    return
  }

  submitState.value = 'submitting'
  submitMessage.value = ''
  try {
    await action({ ...pose })
    submitState.value = 'sent'
    if (mode === 'goal') {
      lastSubmittedGoal = { ...pose }
      lastGoalSubmittedAt = Date.now()
      startGoalTracking(pose)
      draftPose.value = null
      queuedGoalDraft.value = false
      addLandingEffect(pose, 'goal')
      submitMessage.value = '导航目标已提交到后端（状态由前端估算）。'
      if (options.restoreBrowseAfterGoal) interactionMode.value = 'browse'
    } else {
      localizationState.value = 'awaitingConfirmation'
      interactionMode.value = 'browse'
      draftPose.value = null
      queuedGoalDraft.value = false
      addLandingEffect(pose, 'initialPose')
      submitMessage.value = '初始位姿已提交到后端，待人工确认。'
    }
  } catch (error) {
    submitState.value = 'error'
    submitMessage.value = describeNavigationError(error)
  }
}

function rebuildMapBitmap() {
  const frame = props.map
  if (!frame) {
    mapBitmap = null
    situationEvidenceBitmap = null
    mapEvidenceStats.value = { known: 0, occupied: 0, total: 0 }
    mapBounds = null
    mapDataSignature = ''
    fittedMapGeometrySignature = ''
    return
  }

  const geometrySignature = getMapGeometrySignature(frame)
  if (!mapSceneGeometrySignature) mapSceneGeometrySignature = geometrySignature

  const dataSignature = getMapDataSignature(frame)
  if (dataSignature !== mapDataSignature) {
    mapBitmap = createMapBitmap(frame)
    const evidence = createSituationEvidenceBitmap(frame)
    situationEvidenceBitmap = evidence.bitmap
    mapEvidenceStats.value = {
      known: evidence.known,
      occupied: evidence.occupied,
      total: frame.width * frame.height,
    }
    mapDataSignature = dataSignature
  }

  if (geometrySignature !== fittedMapGeometrySignature || !mapBounds) {
    mapBounds = getMapBounds(frame)
    fitView()
  }
}

function rebuildGlobalCostmapBitmap() {
  globalCostmapBitmap = createCostmapBitmap(props.globalCostmap, 'amber')
}

function rebuildLocalCostmapBitmap() {
  localCostmapBitmap = createCostmapBitmap(props.localCostmap, 'red')
}

function rebuildMapPatchBitmap() {
  mapPatchBitmap = createCostmapBitmap(props.mapPatch, 'teal')
}

function flushBitmapRebuild() {
  bitmapRebuildFrame = 0
  if (!stageActive || dirtyBitmaps.size === 0) return

  if (dirtyBitmaps.delete('map')) rebuildMapBitmap()
  if (dirtyBitmaps.delete('globalCostmap')) rebuildGlobalCostmapBitmap()
  if (dirtyBitmaps.delete('localCostmap')) rebuildLocalCostmapBitmap()
  if (dirtyBitmaps.delete('mapPatch')) rebuildMapPatchBitmap()
  scheduleDraw()
}

function scheduleBitmapRebuild(...keys: BitmapKey[]) {
  keys.forEach(key => dirtyBitmaps.add(key))
  if (!stageActive || bitmapRebuildFrame) return
  bitmapRebuildFrame = requestAnimationFrame(flushBitmapRebuild)
}

watch(() => props.map, (next) => {
  if (!next) {
    setInteractionMode('browse')
    followRobot.value = false
    robotSelected.value = false
    selectionBox.value = null
    trajectory.length = 0
    trajectoryCount.value = 0
    stopGoalTracking()
    mapSceneGeometrySignature = ''
  } else {
    const geometrySignature = getMapGeometrySignature(next)
    if (geometrySignature !== mapSceneGeometrySignature) {
      mapSceneGeometrySignature = geometrySignature
      mapSceneVersion.value += 1
    }
  }
  scheduleBitmapRebuild('map')
}, { flush: 'post' })
watch(() => props.mapSource, (source, previous) => {
  if (source === 'live' || previous !== 'live') return
  cancelDraft()
}, { flush: 'post' })
watch(() => props.globalCostmap, () => scheduleBitmapRebuild('globalCostmap'), { flush: 'post' })
watch(() => props.localCostmap, () => scheduleBitmapRebuild('localCostmap'), { flush: 'post' })
watch(() => props.mapPatch, () => scheduleBitmapRebuild('mapPatch'), { flush: 'post' })
watch(situationFieldEnabled, scheduleDraw, { flush: 'post' })
watch([draftPose, interactionMode, localizationState, robotSelected, selectionBox], scheduleDraw, { flush: 'post' })
watch(layers, scheduleDraw, { deep: true, flush: 'post' })
watch(() => props.canNavigate, (canNavigate, wasNavigating) => {
  if (canNavigate || !wasNavigating) return
  localizationState.value = 'needsInitialPose'
  robotSelected.value = false
  cancelDraft()
})
watch(() => props.robotPose, (pose) => {
  if (!pose || ![pose.x, pose.y, pose.theta].every(Number.isFinite)) {
    if (robotSelected.value) robotSelected.value = false
    scheduleDraw()
    return
  }

  const previous = trajectory[trajectory.length - 1]
  if (!previous || Math.hypot(pose.x - previous.x, pose.y - previous.y) >= 0.03) {
    trajectory.push({ x: pose.x, y: pose.y })
    if (trajectory.length > MAX_TRAJECTORY_POINTS) trajectory.splice(0, trajectory.length - MAX_TRAJECTORY_POINTS)
    trajectoryCount.value = trajectory.length
  }

  if (followRobot.value) {
    view.centerX = pose.x
    view.centerY = pose.y
  }

  const goal = trackedGoal.value
  if (goal) {
    const distance = Math.hypot(goal.x - pose.x, goal.y - pose.y)
    if (distance + 0.05 < trackedGoalClosestDistance.value) {
      trackedGoalClosestDistance.value = distance
      trackedGoalLastProgressAt.value = Date.now()
    }
    const start = trackedGoalStartPose.value
    if (start && Math.hypot(pose.x - start.x, pose.y - start.y) >= 0.08) {
      trackedGoalMotionDetected.value = true
    }
  }
  scheduleDraw()
}, { flush: 'post' })
watch(
  () => [props.particles, props.path, props.localPlan, props.scan],
  scheduleDraw,
  { flush: 'post' },
)
watch(
  () => layerDefs.map(({ key }) => {
    const metricKey = layerMetricKeys[key]
    return metricKey ? props.telemetryMetrics?.[metricKey]?.lastAt ?? 0 : 0
  }),
  scheduleDraw,
  { flush: 'post' },
)

function startStageClock() {
  if (stageClockTimer) return
  stageClockTimer = setInterval(refreshStageClock, 1000)
}

function stopStageClock() {
  clearInterval(stageClockTimer)
  stageClockTimer = undefined
}

function activateMapKeyListener() {
  if (mapKeyListenerActive) return
  mapKeyListenerActive = true
  window.addEventListener('keydown', onMapKeyDown, true)
}

function deactivateMapKeyListener() {
  if (!mapKeyListenerActive) return
  mapKeyListenerActive = false
  window.removeEventListener('keydown', onMapKeyDown, true)
}

function cancelScheduledMapWork() {
  if (drawFrame) cancelAnimationFrame(drawFrame)
  drawFrame = 0
  if (bitmapRebuildFrame) cancelAnimationFrame(bitmapRebuildFrame)
  bitmapRebuildFrame = 0
}

onMounted(() => {
  startStageClock()
  reduceMotion = window.matchMedia('(prefers-reduced-motion: reduce)').matches
  activateMapKeyListener()
  context = canvasEl.value?.getContext('2d') ?? null
  if (!context || !hostEl.value) return
  resizeObserver = new ResizeObserver(resize)
  resizeObserver.observe(hostEl.value)
  scheduleBitmapRebuild('map', 'globalCostmap', 'localCostmap', 'mapPatch')
  resize()
})

onActivated(() => {
  stageActive = true
  startStageClock()
  activateMapKeyListener()
  if (!context || !hostEl.value) return
  scheduleBitmapRebuild('map', 'globalCostmap', 'localCostmap', 'mapPatch')
  resize()
  scheduleDraw()
})

onDeactivated(() => {
  stageActive = false
  stopStageClock()
  clearLongPressTimer()
  deactivateMapKeyListener()
  pointerAction = null
  selectionBox.value = null
  cancelScheduledMapWork()
})

onUnmounted(() => {
  stageActive = false
  stopStageClock()
  clearLongPressTimer()
  deactivateMapKeyListener()
  resizeObserver?.disconnect()
  resizeObserver = null
  cancelScheduledMapWork()
  dirtyBitmaps.clear()
  context = null
  mapBitmap = null
  situationEvidenceBitmap = null
  globalCostmapBitmap = null
  localCostmapBitmap = null
  mapPatchBitmap = null
  mapBounds = null
  mapDataSignature = ''
  mapSceneGeometrySignature = ''
  fittedMapGeometrySignature = ''
  pointerAction = null
  selectionBox.value = null
  draftPose.value = null
  queuedGoalDraft.value = false
  landingEffects.length = 0
  trajectory.length = 0
  trajectoryCount.value = 0
  trackedGoal.value = null
})
</script>

<template>
  <section ref="hostEl" class="map-stage" aria-label="机器人现场态势图">
    <canvas
      ref="canvasEl"
      class="map-canvas"
      :class="{
        'has-draft': draftPose,
      }"
      :title="canvasTitle"
      @pointerdown="onPointerDown"
      @pointermove="onPointerMove"
      @pointerup="onPointerUp"
      @pointercancel="onPointerCancel"
      @contextmenu="onContextMenu"
      @wheel.prevent="onWheel"
      @dblclick="onDoubleClick"
    ></canvas>

    <div v-if="map" :key="mapSceneVersion" class="map-scene-frame" aria-hidden="true">
      <span class="map-scene-corner is-tl"></span>
      <span class="map-scene-corner is-tr"></span>
      <span class="map-scene-corner is-br"></span>
      <span class="map-scene-corner is-bl"></span>
    </div>

    <div v-if="map" class="map-readout hud-mono" aria-live="polite">
      <span class="map-readout-dot" :class="{ 'is-cached': isCachedMap }" aria-hidden="true"></span>
      <span class="map-readout-kicker">SITUATION</span>
      <span class="map-readout-meta">{{ situationMeta }}</span>
      <span class="map-readout-mode" :class="`is-${localizationState}`">
        {{ situationActionLabel }}
      </span>
    </div>

    <div v-if="map" class="map-scene-context hud-mono" aria-hidden="true">
      <span>ENV</span>
      <strong>{{ situationContextLabel }}</strong>
    </div>

    <Teleport defer to="#media-drawer-content">
      <section class="media-toolbar map-toolbar" aria-label="地图设置">
        <div class="media-toolbar-head">
          <div class="media-toolbar-heading">
            <span class="media-status-dot" :class="`is-${mapStatusTone}`" aria-hidden="true"></span>
            <span class="media-toolbar-title hud-mono">SITUATION</span>
            <span class="media-toolbar-state hud-mono">{{ stateLabel }}</span>
          </div>
          <div class="media-toolbar-actions">
            <button
              type="button"
              :class="{ active: followRobot }"
              title="跟随机器人"
              aria-label="跟随机器人"
              :aria-pressed="followRobot"
              :disabled="!robotPose"
              @click="toggleFollowRobot"
            >
              <Icon name="lucide:locate-fixed" size="15" />
            </button>
            <button
              type="button"
              title="清空行驶轨迹"
              aria-label="清空行驶轨迹"
              :disabled="trajectoryCount === 0"
              @click="clearTrajectory"
            >
              <Icon name="lucide:eraser" size="15" />
            </button>
            <button type="button" title="适配地图视图" aria-label="适配地图视图" :disabled="!map" @click="fitMap">
              <Icon name="lucide:focus" size="15" />
            </button>
          </div>
        </div>
        <div class="map-toolbar-meta hud-mono">{{ mapMeta }}</div>
        <p v-if="mapStatusNotice" class="map-toolbar-notice" role="status">{{ mapStatusNotice }}</p>
        <section class="map-localization" :class="`is-${localizationState}`" aria-label="定位状态">
          <div class="map-localization-head">
            <span class="map-localization-dot" aria-hidden="true"></span>
            <span class="hud-mono">LOC</span>
            <strong>{{ localizationLabel }}</strong>
          </div>
          <p>{{ localizationHint }}</p>
          <div class="map-localization-actions">
            <button
              v-if="localizationState === 'awaitingConfirmation'"
              type="button"
              class="map-localization-restart"
              :title="initialPoseActionTitle"
              aria-label="重新设置初始位姿"
              :disabled="Boolean(initialPoseBlockReason)"
              @click="beginInitialPose"
            >
              <Icon name="lucide:rotate-ccw" size="13" />
            </button>
            <button
              v-if="localizationState === 'awaitingConfirmation'"
              type="button"
              class="map-localization-confirm"
              title="确认定位稳定并解锁导航"
              @click="confirmLocalization"
            >
              <Icon name="lucide:check" size="13" />
              <span>确认定位</span>
            </button>
            <button
              v-else
              type="button"
              class="map-localization-start"
              :title="initialPoseActionTitle"
              :disabled="Boolean(initialPoseBlockReason)"
              @click="beginInitialPose"
            >
              <Icon name="lucide:locate-fixed" size="13" />
              <span>{{ localizationState === 'ready' ? '重新定位' : '设初始位姿' }}</span>
            </button>
          </div>
        </section>
        <div class="map-toolbar-label map-view-label hud-mono">
          <span>FOCUS</span>
          <strong>{{ mapViewPresetLabel }}</strong>
        </div>
        <div class="map-view-grid" role="group" aria-label="地图观察预设">
          <button
            v-for="preset in viewPresetDefs"
            :key="preset.key"
            type="button"
            class="map-view-toggle"
            :class="{ active: mapViewPreset === preset.key }"
            :aria-pressed="mapViewPreset === preset.key"
            :title="preset.title"
            @click="applyMapViewPreset(preset.key)"
          >
            <Icon :name="preset.icon" size="12" />
            <span>{{ preset.label }}</span>
          </button>
        </div>
        <div class="map-toolbar-label hud-mono">ACTION</div>
        <div class="map-mode-grid" role="group" aria-label="地图交互模式">
          <button
            v-for="mode in modeDefs"
            :key="mode.key"
            type="button"
            class="map-mode-toggle"
            :class="{ active: interactionMode === mode.key }"
            :aria-pressed="interactionMode === mode.key"
            :disabled="isModeDisabled(mode)"
            :title="modeTitle(mode)"
            @click="setInteractionMode(mode.key)"
          >
            <Icon :name="mode.icon" size="12" />
            <span>{{ mode.label }}</span>
          </button>
        </div>

        <div v-if="trackedGoal" class="map-goal-tracker" aria-live="polite">
          <div class="map-goal-tracker-head">
            <span class="hud-mono">ACTION · 前端估算</span>
            <strong>{{ trackedGoalEstimate }}</strong>
          </div>
          <div class="map-goal-tracker-values hud-mono">
            <span>DIST {{ trackedGoalDistance === null ? '--' : `${trackedGoalDistance.toFixed(2)} m` }}</span>
            <span>X {{ formatPoseValue(trackedGoal.x) }}</span>
            <span>Y {{ formatPoseValue(trackedGoal.y) }}</span>
          </div>
          <button type="button" title="停止前端目标追踪" aria-label="停止前端目标追踪" @click="stopGoalTracking">
            <Icon name="lucide:x" size="13" />
            <span>停止追踪</span>
          </button>
        </div>

        <div v-if="interactionMode !== 'browse'" class="map-command" :class="`is-${submitState}`">
          <div v-if="draftPose" class="map-pose-values hud-mono" aria-live="polite">
            <span><em>X</em><strong>{{ formatPoseValue(draftPose.x) }}</strong></span>
            <span><em>Y</em><strong>{{ formatPoseValue(draftPose.y) }}</strong></span>
            <span><em>THETA</em><strong>{{ formatPoseValue(draftPose.theta) }}</strong></span>
          </div>
          <div v-else class="map-command-empty hud-mono">
            <Icon :name="canNavigate ? interactionMode === 'goal' ? 'lucide:crosshair' : 'lucide:locate-fixed' : 'lucide:lock-keyhole'" size="12" />
            <span>{{ canNavigate ? interactionMode === 'goal' ? '目标未设置' : '姿态未设置' : '只读预览' }}</span>
          </div>

          <div class="map-command-actions">
            <button
              type="button"
              class="map-command-cancel"
              title="取消姿态预览"
              aria-label="取消姿态预览"
              :disabled="!draftPose || submitState === 'submitting'"
              @click="cancelDraft"
            >
              <Icon name="lucide:x" size="14" />
            </button>
            <button
              type="button"
              class="map-command-submit"
              :class="{ active: canSubmit }"
              :title="submitTitle"
              :aria-label="submitTitle"
              :disabled="!canSubmit"
              @click="() => void submitDraft()"
            >
              <Icon
                :class="{ 'media-spin': submitState === 'submitting' }"
                :name="submitState === 'submitting' ? 'lucide:loader-circle' : interactionMode === 'goal' ? 'lucide:navigation' : 'lucide:locate-fixed'"
                size="13"
              />
              <span>{{ interactionMode === 'goal' ? queuedGoalDraft ? '提交单个目标' : '提交目标' : '提交位姿' }}</span>
            </button>
          </div>
          <p v-if="submitMessage" class="map-command-feedback" :class="`is-${submitState}`" role="status">
            {{ submitMessage }}
          </p>
        </div>

        <div class="map-toolbar-label hud-mono">DETAIL</div>
        <label
          class="map-situation-field"
          title="态势视图显示真实环境证据、地图覆盖范围、坐标参考与行动信息；不闭合边界；解析视图与导航始终读取原始栅格。"
        >
          <span>态势场</span>
          <input v-model="situationFieldEnabled" type="checkbox" />
          <i aria-hidden="true"></i>
        </label>
        <div class="map-layer-grid" role="group" aria-label="地图图层">
          <button
            v-for="layer in layerDefs"
            :key="layer.key"
            type="button"
            class="map-layer-toggle"
            :class="{ active: layers[layer.key] }"
            :aria-pressed="layers[layer.key]"
            :aria-label="layerToggleTitle(layer)"
            :title="layerToggleTitle(layer)"
            @click="toggleLayer(layer.key)"
          >
            <Icon :name="layer.icon" size="12" />
            <span>{{ layer.label }}</span>
            <i class="map-layer-data" :class="`is-${layerDataState(layer.key)}`" aria-hidden="true"></i>
          </button>
        </div>
      </section>
    </Teleport>

    <div v-if="error && !map" class="map-empty" role="status">
      <Icon name="lucide:map" size="22" />
      <strong>{{ error }}</strong>
    </div>
    <div v-else-if="state === 'connecting' && !map" class="map-empty" role="status">
      <Icon class="media-spin" name="lucide:loader-circle" size="22" />
      <strong>正在同步环境</strong>
    </div>
    <div v-else-if="!map" class="map-empty is-passive" role="status">
      <Icon name="lucide:map" size="22" />
      <strong>{{ mapIssue || '等待环境地图帧' }}</strong>
      <span>{{ mapIssue ? '已自动请求最新环境缓存' : '环境、位姿和行动路径到达后将在此形成态势场景' }}</span>
    </div>
  </section>
</template>

<style>
.map-stage {
  position: absolute;
  inset: 0;
  overflow: hidden;
  background: #dfe7e6;
}

.map-canvas {
  display: block;
  width: 100%;
  height: 100%;
  cursor: default;
  touch-action: none;
}

.map-readout {
  position: absolute;
  left: 18px;
  top: 18px;
  display: inline-flex;
  align-items: center;
  gap: 7px;
  max-width: min(440px, calc(100vw - 150px));
  padding: 6px 9px 6px 8px;
  border-top: 1px solid rgba(37, 99, 235, 0.46);
  border-left: 1px solid color-mix(in srgb, #2563eb 58%, transparent);
  background: rgba(252, 253, 249, 0.78);
  box-shadow: 8px 8px 20px rgba(65, 83, 84, 0.06);
  color: #55636a;
  font-size: 8.5px;
  letter-spacing: 0.06em;
  pointer-events: none;
}

.map-readout-dot {
  width: 5px;
  height: 5px;
  border-radius: 50%;
  background: #16a34a;
  box-shadow: 0 0 0 4px rgba(22, 163, 74, 0.12);
}

.map-readout-dot.is-cached {
  background: #d97706;
  box-shadow: 0 0 0 4px rgba(217, 119, 6, 0.12);
}

.map-readout-kicker {
  color: #1d4ed8;
  font-size: 8px;
}

.map-readout-meta {
  min-width: 0;
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}

.map-readout-mode {
  padding-left: 7px;
  border-left: 1px solid rgba(100, 116, 139, 0.3);
  color: #b45309;
}

.map-readout-mode.is-ready {
  color: #15803d;
}

.map-readout-mode.is-awaitingConfirmation {
  color: #b45309;
}

.map-scene-frame {
  position: absolute;
  inset: 12px;
  pointer-events: none;
  animation: map-scene-frame-in 520ms cubic-bezier(0.22, 1, 0.36, 1) both;
}

.map-scene-corner {
  position: absolute;
  width: 28px;
  height: 28px;
  border-color: rgba(37, 99, 235, 0.34);
  border-style: solid;
}

.map-scene-corner.is-tl {
  top: 0;
  left: 0;
  border-width: 1px 0 0 1px;
}

.map-scene-corner.is-tr {
  top: 0;
  right: 0;
  border-width: 1px 1px 0 0;
}

.map-scene-corner.is-br {
  right: 0;
  bottom: 0;
  border-width: 0 1px 1px 0;
}

.map-scene-corner.is-bl {
  bottom: 0;
  left: 0;
  border-width: 0 0 1px 1px;
}

.map-scene-context {
  position: absolute;
  right: 18px;
  bottom: 18px;
  display: inline-flex;
  align-items: center;
  gap: 7px;
  padding: 5px 7px;
  border-right: 1px solid rgba(13, 148, 136, 0.5);
  background: rgba(252, 253, 249, 0.64);
  color: #64748b;
  font-size: 8px;
  letter-spacing: 0.06em;
  pointer-events: none;
}

.map-scene-context strong {
  color: #0f766e;
  font-weight: 500;
}

@keyframes map-scene-frame-in {
  from {
    opacity: 0;
    transform: scale(0.986);
  }
  to {
    opacity: 1;
    transform: scale(1);
  }
}

.map-toolbar-meta {
  overflow: hidden;
  color: var(--muted-foreground);
  font-size: 8.5px;
  text-overflow: ellipsis;
  white-space: nowrap;
}

.map-toolbar-notice {
  margin: -1px 0 1px;
  color: #a16207;
  font-size: 8.5px;
  line-height: 1.45;
}

.media-status-dot.is-cached {
  background: #d97706;
}

.map-localization {
  display: grid;
  gap: 6px;
  padding: 9px 0;
  border-top: 1px solid color-mix(in srgb, var(--border) 72%, transparent);
  border-bottom: 1px solid color-mix(in srgb, var(--border) 72%, transparent);
  transition: border-color 180ms ease;
}

.map-localization.is-awaitingConfirmation {
  border-color: color-mix(in srgb, #d97706 34%, var(--border));
}

.map-localization.is-ready {
  border-color: color-mix(in srgb, #15803d 32%, var(--border));
}

.map-localization-head,
.map-localization-actions {
  display: flex;
  align-items: center;
}

.map-localization-head {
  gap: 6px;
}

.map-localization-head .hud-mono {
  color: var(--ornament);
  font-size: 8px;
}

.map-localization-head strong {
  color: var(--foreground);
  font-size: 9px;
  font-weight: 500;
}

.map-localization-dot {
  width: 5px;
  height: 5px;
  flex: none;
  border-radius: 50%;
  background: #d97706;
  box-shadow: 0 0 0 3px color-mix(in srgb, #d97706 10%, transparent);
  transition: background-color 180ms ease, box-shadow 180ms ease;
}

.map-localization.is-ready .map-localization-dot {
  background: #16a34a;
  box-shadow: 0 0 0 3px color-mix(in srgb, #16a34a 11%, transparent);
}

.map-localization p {
  margin: 0;
  color: var(--muted-foreground);
  font-size: 8.5px;
  line-height: 1.45;
}

.map-localization-actions {
  gap: 6px;
}

.map-toolbar .map-localization-start,
.map-toolbar .map-localization-confirm {
  width: auto;
  height: 28px;
  gap: 5px;
  padding: 0 8px;
  border: 1px solid var(--border);
  color: var(--foreground);
  font-size: 8.5px;
  transition: border-color 180ms ease, background-color 180ms ease, color 180ms ease, transform 180ms ease;
}

.map-toolbar .map-localization-start {
  border-color: color-mix(in srgb, #0f766e 38%, var(--border));
  color: #0f766e;
}

.map-toolbar .map-localization-confirm {
  border-color: color-mix(in srgb, #15803d 44%, var(--border));
  color: #15803d;
}

.map-toolbar .map-localization-restart {
  width: 28px;
  height: 28px;
  border: 1px solid var(--border);
}

.map-toolbar .map-localization-start:active:not(:disabled),
.map-toolbar .map-localization-confirm:active:not(:disabled),
.map-toolbar .map-localization-restart:active:not(:disabled) {
  transform: translateY(1px);
}

.map-toolbar .media-toolbar-actions button.active {
  background: color-mix(in srgb, var(--primary) 11%, transparent);
  color: var(--primary);
}

.map-toolbar-label {
  margin-bottom: -7px;
  color: var(--ornament);
  font-size: 8px;
}

.map-view-label {
  display: flex;
  align-items: center;
  justify-content: space-between;
}

.map-view-label strong {
  color: var(--muted-foreground);
  font-size: 7.5px;
  font-weight: 500;
}

.map-view-grid {
  display: grid;
  grid-template-columns: repeat(2, minmax(0, 1fr));
  gap: 4px;
}

.map-toolbar .map-view-toggle {
  width: 100%;
  height: 30px;
  min-width: 0;
  gap: 5px;
  border: 1px solid var(--border);
  color: var(--muted-foreground);
  font-size: 8.5px;
  transition:
    border-color 0.16s ease,
    background-color 0.16s ease,
    color 0.16s ease,
    transform 0.16s ease;
}

.map-view-toggle span,
.map-mode-toggle span {
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}

.map-view-toggle.active {
  border-color: color-mix(in srgb, var(--primary) 52%, var(--border));
  background: color-mix(in srgb, var(--primary) 9%, transparent);
  color: var(--primary);
}

.map-view-toggle:active:not(:disabled) {
  transform: translateY(1px);
}

.map-mode-grid {
  display: grid;
  grid-template-columns: repeat(3, minmax(0, 1fr));
  gap: 4px;
}

.map-toolbar .map-mode-toggle {
  width: 100%;
  height: 30px;
  min-width: 0;
  gap: 4px;
  border: 1px solid var(--border);
  color: var(--muted-foreground);
  font-size: 8.5px;
  transition:
    border-color 0.16s ease,
    background-color 0.16s ease,
    color 0.16s ease,
    transform 0.16s ease;
}

.map-mode-toggle.active {
  border-color: color-mix(in srgb, var(--primary) 52%, var(--border));
  background: color-mix(in srgb, var(--primary) 9%, transparent);
  color: var(--primary);
}

.map-mode-toggle:active:not(:disabled) {
  transform: translateY(1px);
}

.map-goal-tracker {
  display: grid;
  gap: 7px;
  padding: 9px 0;
  border-top: 1px solid color-mix(in srgb, #d97706 34%, transparent);
  border-bottom: 1px solid color-mix(in srgb, #d97706 24%, transparent);
}

.map-goal-tracker-head,
.map-goal-tracker-values {
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 7px;
}

.map-goal-tracker-head span {
  color: #b45309;
  font-size: 7.5px;
}

.map-goal-tracker-head strong {
  color: var(--foreground);
  font-size: 9px;
  font-weight: 500;
}

.map-goal-tracker-values {
  color: var(--muted-foreground);
  font-size: 7.5px;
}

.map-toolbar .map-goal-tracker button {
  width: auto;
  height: 26px;
  justify-self: start;
  gap: 5px;
  padding: 0 7px;
  border: 1px solid var(--border);
  font-size: 8px;
}

.map-command {
  display: grid;
  gap: 8px;
  padding: 9px 0;
  border-top: 1px solid color-mix(in srgb, var(--border) 72%, transparent);
  border-bottom: 1px solid color-mix(in srgb, var(--border) 72%, transparent);
}

.map-pose-values {
  display: grid;
  grid-template-columns: repeat(3, minmax(0, 1fr));
  gap: 6px;
}

.map-pose-values span {
  display: grid;
  min-width: 0;
  gap: 2px;
}

.map-pose-values em {
  color: var(--ornament);
  font-size: 7.5px;
  font-style: normal;
}

.map-pose-values strong {
  overflow: hidden;
  color: var(--foreground);
  font-size: 10px;
  font-weight: 500;
  text-overflow: ellipsis;
  white-space: nowrap;
}

.map-command-empty {
  display: flex;
  align-items: center;
  gap: 6px;
  min-height: 25px;
  color: var(--muted-foreground);
  font-size: 8.5px;
}

.map-command-actions {
  display: flex;
  gap: 6px;
}

.map-toolbar .map-command-cancel {
  width: 30px;
  height: 30px;
  border: 1px solid var(--border);
}

.map-toolbar .map-command-submit {
  width: auto;
  height: 30px;
  flex: 1 1 auto;
  gap: 6px;
  border: 1px solid var(--border);
  padding: 0 9px;
  font-size: 9px;
}

.map-command-submit.active {
  border-color: color-mix(in srgb, var(--primary) 54%, var(--border));
  background: color-mix(in srgb, var(--primary) 11%, transparent);
  color: var(--primary);
}

.map-command-feedback {
  margin: 0;
  color: var(--muted-foreground);
  font-size: 8.5px;
  line-height: 1.45;
  overflow-wrap: anywhere;
}

.map-command-feedback.is-sent {
  color: #15803d;
}

.map-command-feedback.is-error {
  color: #dc2626;
}

.map-layer-grid {
  display: grid;
  grid-template-columns: repeat(2, minmax(0, 1fr));
  gap: 4px;
}

.map-situation-field {
  display: flex;
  align-items: center;
  justify-content: space-between;
  min-height: 27px;
  padding: 0 7px;
  border: 1px solid var(--border);
  color: var(--muted-foreground);
  cursor: pointer;
  font-size: 8.5px;
  transition:
    border-color 0.16s ease,
    background-color 0.16s ease,
    color 0.16s ease;
}

.map-situation-field:focus-within {
  border-color: color-mix(in srgb, var(--primary) 52%, var(--border));
}

.map-situation-field input {
  position: absolute;
  width: 1px;
  height: 1px;
  margin: -1px;
  clip: rect(0 0 0 0);
  clip-path: inset(50%);
  overflow: hidden;
  white-space: nowrap;
}

.map-situation-field i {
  position: relative;
  width: 24px;
  height: 13px;
  border: 1px solid var(--border);
  background: color-mix(in srgb, var(--muted) 46%, transparent);
  transition:
    background-color 0.18s ease,
    border-color 0.18s ease;
}

.map-situation-field i::after {
  position: absolute;
  top: 2px;
  left: 2px;
  width: 7px;
  height: 7px;
  background: var(--muted-foreground);
  content: '';
  transition:
    transform 0.2s cubic-bezier(0.2, 0.8, 0.2, 1),
    background-color 0.18s ease;
}

.map-situation-field input:checked + i {
  border-color: color-mix(in srgb, var(--primary) 60%, var(--border));
  background: color-mix(in srgb, var(--primary) 16%, transparent);
}

.map-situation-field input:checked + i::after {
  transform: translateX(11px);
  background: var(--primary);
}

.map-situation-field input:focus-visible + i {
  outline: 1px solid var(--primary);
  outline-offset: 2px;
}

.map-layer-toggle {
  display: inline-flex;
  align-items: center;
  justify-content: flex-start;
  gap: 5px;
  min-width: 0;
  min-height: 27px;
  padding: 0 6px;
  border: 1px solid var(--border);
  background: transparent;
  color: var(--muted-foreground);
  font-size: 8.5px;
  white-space: nowrap;
}

/* 媒体工具条的图标按钮默认固定 28px，图层按钮需要撑满两列并保留文字。 */
.map-toolbar .map-layer-toggle {
  width: 100%;
  height: 27px;
  flex: 1 1 auto;
}

.map-layer-toggle span {
  overflow: hidden;
  text-overflow: ellipsis;
}

.map-layer-data {
  width: 4px;
  height: 4px;
  margin-left: auto;
  flex: none;
  border-radius: 50%;
  background: #98a2b3;
  opacity: 0.45;
  transition: background-color 180ms ease, box-shadow 180ms ease, opacity 180ms ease;
}

.map-layer-data.is-fresh {
  background: #22c55e;
  box-shadow: 0 0 0 3px color-mix(in srgb, #22c55e 10%, transparent);
  opacity: 1;
}

.map-layer-data.is-stale {
  background: #f59e0b;
  box-shadow: 0 0 0 3px color-mix(in srgb, #f59e0b 12%, transparent);
  opacity: 0.92;
}

.map-layer-toggle.active {
  border-color: color-mix(in srgb, var(--primary) 48%, var(--border));
  background: color-mix(in srgb, var(--primary) 10%, transparent);
  color: var(--primary);
}

.map-empty {
  position: absolute;
  inset: 0;
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  gap: 8px;
  padding: 80px 24px;
  color: #64748b;
  text-align: center;
  pointer-events: none;
}

.map-empty strong {
  max-width: min(360px, 80vw);
  color: #334155;
  font-size: 13px;
  font-weight: 500;
}

.map-empty span {
  max-width: min(360px, 80vw);
  font-size: 11px;
}

@media (max-width: 520px) {
  .map-readout {
    left: 12px;
    top: 12px;
    max-width: calc(100vw - 120px);
  }

  .map-scene-frame {
    inset: 8px;
  }

  .map-scene-corner {
    width: 20px;
    height: 20px;
  }

  .map-scene-context {
    right: 12px;
    bottom: 12px;
  }
}

@media (prefers-reduced-motion: reduce) {
  .map-canvas {
    scroll-behavior: auto;
  }

  .map-mode-toggle {
    transition: none;
  }

  .map-localization,
  .map-localization-dot,
  .map-toolbar .map-localization-start,
  .map-toolbar .map-localization-confirm,
  .map-toolbar .map-localization-restart {
    transition: none;
  }

  .map-scene-frame {
    animation: none;
  }
}
</style>
