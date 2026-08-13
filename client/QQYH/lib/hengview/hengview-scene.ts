/**
 * /hengview 三维内核（框架无关）
 *
 * 单资产架构：mzz01-cq-explode.glb（完整 1,675 节点装配层级）承担全页：
 * - 开机：全息鬼影 → 扫描面自下而上物化为真实 PBR
 * - 滚动叙事：云台特写 → 三级顶升真实行程 → 舵轮全向转向 → 总成级结构分解 → 收束
 * - 工程遥测：顶升高度 / 分解进度 / 段位实时回调给 UI
 *
 * Vue 只负责覆盖层与生命周期，本模块可独立调试。
 */
import * as THREE from 'three'
import { GLTFLoader } from 'three/examples/jsm/loaders/GLTFLoader.js'
import { MeshoptDecoder } from 'three/examples/jsm/libs/meshopt_decoder.module.js'
import { RoomEnvironment } from 'three/examples/jsm/environments/RoomEnvironment.js'
import { MZZ01_MECHANISM_PROFILE } from '../../data/mzz01-mechanism-profile'
import { regradeModelMaterials, type HoloUniforms } from './hengview-materials'
import { HengviewPost } from './hengview-post'

export type HengviewPhase = 'loading' | 'boot' | 'live'

export interface HengviewAnchor {
  id: string
  code: string
  label: string
  x: number
  y: number
  visible: boolean
}

export interface HengviewTelemetry {
  section: number
  scanPct: number
  liftMeters: number
  explodePct: number
  online: boolean
}

export interface HengviewSceneOptions {
  reducedMotion?: boolean
  onProgress?: (value: number) => void
  onReady?: () => void
  onPhase?: (phase: HengviewPhase) => void
  onTelemetry?: (telemetry: HengviewTelemetry) => void
  onAnchors?: (anchors: HengviewAnchor[]) => void
  onError?: (message: string) => void
}

const MODEL_URL = '/models/mzz01-cq-explode.glb'
const VOID_COLOR = 0x05070A
const LIFT_RANGE_METERS = 2.0

/** 灯光基准（暗厅；applyLighting 按主题与开机系数合成） */
const KEY_INTENSITY = 118
const RIM_INTENSITY = 2.0
const HEMI_INTENSITY = 0.62
const ENV_INTENSITY = 0.5
const BASE_EXPOSURE = 1.3

/** 日夜主题色表 */
const THEME_VOID_DARK = new THREE.Color(VOID_COLOR)
const THEME_VOID_LIGHT = new THREE.Color(0xC8CDD2)
const THEME_WALL_DARK = new THREE.Color(0x080C11)
const THEME_WALL_LIGHT = new THREE.Color(0x7A8189) // 压暗：必须比灯带更暗，避免背景抢镜
const THEME_FLOOR_DARK = new THREE.Color(0x06080D)
const THEME_FLOOR_LIGHT = new THREE.Color(0xD4D8DC) // 提亮：白厅地面不能有黑色脏斑
const THEME_WALL_TEXT_DARK = new THREE.Color(0x1A252E)
const THEME_WALL_TEXT_LIGHT = new THREE.Color(0x485058)
const THEME_STEEL_DARK = new THREE.Color(0x53687A)
const THEME_STEEL_LIGHT = new THREE.Color(0x39434D)
const THEME_CYAN_DARK = new THREE.Color(0x46D7EA)
const THEME_CYAN_LIGHT = new THREE.Color(0x0E96AA)
const THEME_STRIP_DARK = new THREE.Color(0.74, 0.8, 0.84)
const THEME_STRIP_LIGHT = new THREE.Color(0.98, 1.04, 1.1)
const THEME_HEMI_SKY_DARK = new THREE.Color(0x2C3741)
const THEME_HEMI_SKY_LIGHT = new THREE.Color(0xE8EEF3)
const THEME_HEMI_GROUND_DARK = new THREE.Color(0x070A0E)
const THEME_HEMI_GROUND_LIGHT = new THREE.Color(0x9CA4AC)

/** 滚动分段边界（与页面文案窗口共享） */
export const HENGVIEW_SECTIONS = [0.14, 0.34, 0.64, 0.76, 0.94] as const

const clamp01 = (value: number) => Math.min(1, Math.max(0, value))
const smoothstep = (edge0: number, edge1: number, value: number) => {
  const t = clamp01((value - edge0) / (edge1 - edge0))
  return t * t * (3 - 2 * t)
}
const easeInOutCubic = (value: number) => {
  const t = clamp01(value)
  return t < 0.5 ? 4 * t ** 3 : 1 - (-2 * t + 2) ** 3 / 2
}
const easeOutCubic = (value: number) => 1 - (1 - clamp01(value)) ** 3

interface CameraKey {
  at: number
  yaw: number
  k: number
  camH: number
  focH: number
}

/** 镜头关键帧：yaw 连续取值（度），k 为取景距离系数，camH/focH 为有效高度比例 */
const CAMERA_KEYS: CameraKey[] = [
  { at: 0.0, yaw: 24, k: 1.3, camH: 0.42, focH: 0.5 },
  { at: 0.16, yaw: 24, k: 1.3, camH: 0.42, focH: 0.5 },
  { at: 0.235, yaw: -16, k: 0.52, camH: 0.92, focH: 0.94 },
  { at: 0.315, yaw: -26, k: 0.55, camH: 0.88, focH: 0.92 },
  { at: 0.42, yaw: 38, k: 1.08, camH: 0.5, focH: 0.55 },
  { at: 0.545, yaw: 48, k: 1.02, camH: 0.6, focH: 0.6 },
  { at: 0.65, yaw: -40, k: 0.58, camH: 0.1, focH: 0.15 },
  { at: 0.75, yaw: -56, k: 0.6, camH: 0.12, focH: 0.17 },
  { at: 0.845, yaw: 12, k: 0.92, camH: 0.55, focH: 0.52 },
  { at: 0.93, yaw: 18, k: 1.34, camH: 0.52, focH: 0.5 },
  { at: 1.0, yaw: 26, k: 1.2, camH: 0.44, focH: 0.52 },
]

const GROUND_VERTEX = /* glsl */ `
varying vec2 vWorldXZ;
#include <fog_pars_vertex>
void main() {
  vec4 worldPosition = modelMatrix * vec4(position, 1.0);
  vWorldXZ = worldPosition.xz;
  vec4 mvPosition = viewMatrix * worldPosition;
  gl_Position = projectionMatrix * mvPosition;
  #include <fog_vertex>
}
`

/** 工业标定台标线层：十字准线 + 角度刻度校准环 + 旋转扫掠弧 + 方形台界 + 稀疏点阵 */
const GROUND_FRAGMENT = /* glsl */ `
varying vec2 vWorldXZ;
uniform vec3 uSteel;
uniform vec3 uCyan;
uniform float uR;
uniform float uTime;
uniform float uMarkFade;
uniform float uBootRingRadius;
uniform float uBootRingAlpha;
#include <fog_pars_fragment>

float lineMask(float coord) {
  float g = abs(fract(coord - 0.5) - 0.5) / fwidth(coord);
  return 1.0 - min(g, 1.0);
}

float bandMask(float value, float halfWidth) {
  float aa = fwidth(value);
  return 1.0 - smoothstep(halfWidth - aa, halfWidth + aa, abs(value));
}

void main() {
  vec2 p = vWorldXZ;
  float r = length(p);
  float angle = atan(p.y, p.x);
  float deg = degrees(angle);

  float marks = 0.0;
  float cyanMarks = 0.0;

  // 中心十字准线（限制在 0.55R 内，端点留缺口）
  float crossLen = uR * 0.55;
  float crossGap = uR * 0.1;
  float crossA = bandMask(p.x, uR * 0.0045) * step(abs(p.y), crossLen) * step(crossGap, abs(p.y));
  float crossB = bandMask(p.y, uR * 0.0045) * step(abs(p.x), crossLen) * step(crossGap, abs(p.x));
  marks += (crossA + crossB) * 0.62;

  // 校准环 1.32R + 每6°短刻度 + 每30°长刻度
  float ringR = uR * 1.32;
  marks += bandMask(r - ringR, uR * 0.006) * 0.8;
  float tick6 = lineMask(deg / 6.0);
  float tick30 = lineMask(deg / 30.0);
  float bandShort = step(uR * 1.24, r) * step(r, uR * 1.3);
  float bandLong = step(uR * 1.16, r) * step(r, uR * 1.3);
  marks += tick6 * bandShort * 0.42 + tick30 * bandLong * 0.7;

  // 校准环上的旋转扫掠亮弧（约42°）
  float sweepDelta = mod(angle - uTime * 0.22 + 3.14159265, 6.2831853) - 3.14159265;
  cyanMarks += smoothstep(0.42, 0.0, abs(sweepDelta)) * bandMask(r - ringR, uR * 0.013) * 0.85;

  // 方形试验台界 2.35R
  float sq = max(abs(p.x), abs(p.y));
  marks += bandMask(sq - uR * 2.35, uR * 0.005) * 0.38;

  // 外围稀疏点阵（2.6R–6R）
  vec2 cell = fract(p / (uR * 0.55)) - 0.5;
  float dots = 1.0 - smoothstep(0.03, 0.07, length(cell));
  float dotZone = smoothstep(uR * 2.45, uR * 3.1, r) * (1.0 - smoothstep(uR * 5.0, uR * 6.6, r));
  marks += dots * dotZone * 0.5;

  // 开机扫描环
  cyanMarks += bandMask(r - uBootRingRadius, uR * 0.05) * uBootRingAlpha;

  float fade = exp(-r / (uR * 5.2));
  float total = marks + cyanMarks;
  vec3 color = (uSteel * marks + uCyan * cyanMarks) / max(total, 0.0001);
  float alpha = min(total, 1.0) * fade * uMarkFade;

  gl_FragColor = vec4(color, alpha);
  #include <fog_fragment>
}
`

/**
 * 流动星光：速度拉伸条痕粒子。
 * 每颗星是一个实例化四边形，顶点着色器把「当前位置」与「uTrail 秒前位置」
 * 投影到屏幕像素空间，沿运动方向拉成连续光痕（头亮尾消），静止时退化为圆点。
 */
const STAR_VERTEX = /* glsl */ `
attribute float aRadius;
attribute float aBaseY;
attribute float aPhase;
attribute float aSpeed;
attribute float aSize;
attribute float aHue;
uniform float uTime;
uniform float uTrail;
uniform vec2 uViewport;
uniform float uPixelRatio;
uniform float uScale;
varying vec2 vCorner;
varying float vHue;
varying float vTwinkle;

vec3 starPos(float t) {
  float angle = aPhase + t * aSpeed;
  float y = aBaseY + sin(t * 0.38 + aPhase * 4.7) * aRadius * 0.045;
  return vec3(cos(angle) * aRadius, y, sin(angle) * aRadius);
}

void main() {
  vec4 c0 = projectionMatrix * modelViewMatrix * vec4(starPos(uTime), 1.0);
  vec4 c1 = projectionMatrix * modelViewMatrix * vec4(starPos(uTime - uTrail), 1.0);
  vCorner = position.xy;
  vHue = aHue;
  vTwinkle = 0.6 + 0.4 * sin(uTime * (0.7 + aHue * 1.6) + aPhase * 11.0);

  if (c0.w <= 0.05 || c1.w <= 0.05) {
    gl_Position = vec4(0.0, 0.0, 2.0, 1.0);
    return;
  }

  vec2 halfVp = uViewport * 0.5;
  vec2 px0 = c0.xy / c0.w * halfVp;
  vec2 px1 = c1.xy / c1.w * halfVp;
  vec2 delta = px1 - px0;
  float len = length(delta);
  vec2 dir = len > 0.0001 ? delta / len : vec2(1.0, 0.0);
  vec2 nrm = vec2(-dir.y, dir.x);

  float width = aSize * uPixelRatio * clamp(uScale / c0.w, 0.18, 1.6);
  len = min(len, width * 5.0);

  vec2 centerPx = px0 + dir * len * 0.5;
  vec2 cornerPx = centerPx
    + dir * position.x * (len + width * 1.5)
    + nrm * position.y * width * 1.5;

  gl_Position = vec4(cornerPx / halfVp * c0.w, c0.z, c0.w);
}
`

const STAR_FRAGMENT = /* glsl */ `
uniform float uOpacity;
varying vec2 vCorner;
varying float vHue;
varying float vTwinkle;

void main() {
  // 轴向 0 = 头部（当前位置），1 = 尾部（滞后位置）
  float axial = vCorner.x + 0.5;
  float lateral = vCorner.y * 2.0;
  float radial = exp(-lateral * lateral * 3.4);
  float head = exp(-(axial - 0.06) * (axial - 0.06) * 60.0) * 0.85;
  float tail = (1.0 - smoothstep(0.02, 0.94, axial)) * 0.26;
  float a = radial * max(head, tail) * vTwinkle * uOpacity;
  if (a < 0.004) discard;
  vec3 col = mix(vec3(0.56, 0.66, 0.74), vec3(0.78, 0.86, 0.9), vHue);
  gl_FragColor = vec4(col * a, a);
}
`

/**
 * 背墙金属字的冷白掠光：仅用文字 alpha 作为遮罩。
 * 它是缓慢移动的表面反射，不额外提高 Bloom，也不使用叠加混合或呼吸脉冲。
 */
const WALL_TEXT_SHEEN_VERTEX = /* glsl */ `
varying vec2 vUv;

void main() {
  vUv = uv;
  gl_Position = projectionMatrix * modelViewMatrix * vec4(position, 1.0);
}
`

const WALL_TEXT_SHEEN_FRAGMENT = /* glsl */ `
uniform sampler2D uMap;
uniform float uTime;
uniform float uOpacity;
varying vec2 vUv;

float softBand(float value, float center, float width) {
  return 1.0 - smoothstep(width * 0.48, width, abs(value - center));
}

void main() {
  float mask = texture2D(uMap, vUv).a;
  if (mask < 0.02) discard;

  // 约 32 秒完成一次非对称斜向掠过；副带只留下极细的金属纹理层次。
  float travel = fract(vUv.x * 0.84 - vUv.y * 0.17 - uTime * 0.031);
  float broadSheen = softBand(travel, 0.46, 0.27);
  float fineSheen = softBand(fract(travel + 0.42), 0.5, 0.09) * 0.24;
  float sheen = broadSheen + fineSheen;
  float alpha = mask * sheen * uOpacity;
  if (alpha < 0.001) discard;

  vec3 steel = mix(vec3(0.34, 0.42, 0.48), vec3(0.58, 0.67, 0.74), broadSheen);
  gl_FragColor = vec4(steel, alpha);
}
`

interface MovableEntry {
  object: THREE.Object3D
  initialPosition: THREE.Vector3
}

interface RetractableGuideEntry extends MovableEntry {
  initialScale: THREE.Vector3
  initialQuaternion: THREE.Quaternion
  localAxis: THREE.Vector3
  axisIndex: 0 | 1 | 2
  axisMin: number
  worldLength: number
}

interface WheelEntry extends MovableEntry {
  initialQuaternion: THREE.Quaternion
  pivot: THREE.Vector3
}

interface RenderProfile {
  compact: boolean
  reducedMotion: boolean
  basePixelRatio: number
  multisampling: number
  shadows: boolean
}

export class HengviewScene {
  private host: HTMLElement
  private callbacks: Required<HengviewSceneOptions>
  private profile: RenderProfile

  private renderer: THREE.WebGLRenderer | null = null
  private scene: THREE.Scene | null = null
  private camera: THREE.PerspectiveCamera | null = null
  private post: HengviewPost | null = null
  private pmremTexture: THREE.Texture | null = null

  private model: THREE.Object3D | null = null
  private modelGroup: THREE.Group | null = null
  private holo: HoloUniforms | null = null
  private eyeMaterials: THREE.MeshPhysicalMaterial[] = []

  private keyLight: THREE.SpotLight | null = null
  private rimLight: THREE.DirectionalLight | null = null
  private hemiLight: THREE.HemisphereLight | null = null
  private groundMaterial: THREE.ShaderMaterial | null = null
  private orbitLightA: THREE.PointLight | null = null
  private orbitLightB: THREE.PointLight | null = null
  private starMaterial: THREE.ShaderMaterial | null = null
  private stripMaterial: THREE.MeshBasicMaterial | null = null
  private wallGrazingLight: THREE.SpotLight | null = null
  private wallMaterial: THREE.MeshStandardMaterial | null = null
  private wallTextMaterial: THREE.MeshStandardMaterial | null = null
  private wallTextSheenMaterial: THREE.ShaderMaterial | null = null
  private floorBaseMaterial: THREE.MeshBasicMaterial | null = null
  private catcherMaterial: THREE.ShadowMaterial | null = null
  private contactMaterial: THREE.MeshBasicMaterial | null = null

  // 日夜主题：0 = 暗厅，1 = 白厅
  private themeTarget = 0
  private themeMix = 0
  private themeMixEased = 0
  private stripPulse = 0
  private lightScale = 0
  private sceneFit = 10
  private showroomHalfWidth = 10
  private showroomBackZ = -10
  private showroomFrontZ = 10

  private modelRadius = 1
  private modelHeight = 1
  private modelMinY = 0
  private modelMaxY = 1

  private midStage: MovableEntry | null = null
  private topStage: MovableEntry | null = null
  private followers: MovableEntry[] = []
  private topStageSurfaceFollowers: MovableEntry[] = []
  private retractableGuides: RetractableGuideEntry[] = []
  private wheels: WheelEntry[] = []
  private anchorNodes = new Map<string, THREE.Object3D>()

  private phase: HengviewPhase = 'loading'
  private destroyed = false
  private loaded = false
  private animationFrame: number | null = null
  private lastFrameTime = 0
  private elapsed = 0

  private bootStartedAt: number | null = null
  /** 调试辅助：URL 带 ?slowboot 时开机序列放慢 3 倍，便于视觉调优 */
  private bootTimeScale = typeof location !== 'undefined' && location.search.includes('slowboot') ? 3 : 1
  private bootScanProgress = 0
  private bloomBoost = 0
  private exposureBoost = 0
  private baseExposure = BASE_EXPOSURE

  private scrollTarget = 0
  private scroll = 0

  private telemetryAt = 0
  private anchorsAt = 0
  private lastAnchorsVisible = false
  private lastPoseActive = false
  private sliceGhost = 0
  private sliceTravel = 0

  // 画质治理
  private frameCount = 0
  private frameAccum = 0
  private qualityTier = 0
  private recoverStreak = 0

  private cameraPosition = new THREE.Vector3()
  private cameraFocus = new THREE.Vector3()
  private cameraDesired = new THREE.Vector3()
  private cameraFocusDesired = new THREE.Vector3()
  private tempVecA = new THREE.Vector3()
  private tempVecB = new THREE.Vector3()
  private tempQuat = new THREE.Quaternion()
  private resizeObserver: ResizeObserver | null = null

  constructor(host: HTMLElement, options: HengviewSceneOptions = {}) {
    this.host = host

    const compact = window.matchMedia('(max-width: 700px)').matches
    const reducedMotion = Boolean(
      options.reducedMotion ?? window.matchMedia('(prefers-reduced-motion: reduce)').matches,
    )
    this.profile = {
      compact,
      reducedMotion,
      basePixelRatio: Math.min(window.devicePixelRatio, compact ? 1.15 : 1.5),
      multisampling: compact ? 0 : 4,
      shadows: !compact,
    }

    this.callbacks = {
      reducedMotion,
      onProgress: options.onProgress ?? (() => {}),
      onReady: options.onReady ?? (() => {}),
      onPhase: options.onPhase ?? (() => {}),
      onTelemetry: options.onTelemetry ?? (() => {}),
      onAnchors: options.onAnchors ?? (() => {}),
      onError: options.onError ?? (() => {}),
    }
  }

  get isCompact() {
    return this.profile.compact
  }

  /** 开发期诊断：装配树捕获情况 */
  get debugInfo() {
    return {
      followers: this.followers.map((entry) => entry.object.name),
      topStageSurfaceFollowers: this.topStageSurfaceFollowers.map((entry) => entry.object.name),
      retractableGuides: this.retractableGuides.map((entry) => entry.object.name),
      wheels: this.wheels.length,
      midStage: this.midStage?.object.name ?? null,
      topStage: this.topStage?.object.name ?? null,
    }
  }

  async start() {
    try {
      this.createRenderer()
      this.createEnvironment()
      await this.loadModel()
      if (this.destroyed) return
      this.callbacks.onProgress(0.8)

      await this.prewarm()
      if (this.destroyed) return

      this.loaded = true
      this.callbacks.onProgress(1)
      this.startAnimation()
      this.callbacks.onReady()
    } catch (error) {
      if (!this.destroyed) {
        this.callbacks.onError(error instanceof Error ? error.message : 'SCENE INITIALIZATION FAILED')
      }
    }
  }

  setScrollProgress(value: number) {
    this.scrollTarget = clamp01(value)
  }

  /** 页面加载遮罩收起后调用：启动全息扫描物化 */
  beginBoot() {
    if (this.destroyed || !this.loaded || this.bootStartedAt !== null) return

    if (this.profile.reducedMotion) {
      this.completeBoot(true)
      return
    }

    this.bootStartedAt = performance.now() / 1000
    this.setPhase('boot')
  }

  private setPhase(phase: HengviewPhase) {
    if (this.phase === phase) return
    this.phase = phase
    this.callbacks.onPhase(phase)
  }

  private createRenderer() {
    this.scene = new THREE.Scene()
    this.scene.background = new THREE.Color(VOID_COLOR)

    this.camera = new THREE.PerspectiveCamera(this.profile.compact ? 38 : 30, 1, 0.05, 220)

    this.renderer = new THREE.WebGLRenderer({
      antialias: false,
      powerPreference: 'high-performance',
    })
    this.renderer.outputColorSpace = THREE.SRGBColorSpace
    this.renderer.toneMapping = THREE.AgXToneMapping
    this.renderer.toneMappingExposure = this.baseExposure
    this.renderer.setClearColor(VOID_COLOR, 1)
    this.renderer.setPixelRatio(this.profile.basePixelRatio)
    if (this.profile.shadows) {
      this.renderer.shadowMap.enabled = true
      this.renderer.shadowMap.type = THREE.PCFShadowMap
    }
    this.host.append(this.renderer.domElement)

    this.resizeObserver = new ResizeObserver(() => this.resize())
    this.resizeObserver.observe(this.host)
    document.addEventListener('visibilitychange', this.handleVisibilityChange)
    this.resize()
  }

  private createEnvironment() {
    if (!this.renderer || !this.scene) return

    const pmrem = new THREE.PMREMGenerator(this.renderer)
    const environmentScene = new RoomEnvironment()
    this.pmremTexture = pmrem.fromScene(environmentScene, 0.04).texture
    this.scene.environment = this.pmremTexture
    this.scene.environmentIntensity = ENV_INTENSITY
    pmrem.dispose()

    this.hemiLight = new THREE.HemisphereLight(0x4A5866, 0x0A0E14, HEMI_INTENSITY)
    this.scene.add(this.hemiLight)

    this.keyLight = new THREE.SpotLight(0xEDF4FA, KEY_INTENSITY, 0, 0.52, 0.74, 2)
    this.scene.add(this.keyLight, this.keyLight.target)

    this.rimLight = new THREE.DirectionalLight(0xB6E0EE, RIM_INTENSITY)
    this.scene.add(this.rimLight, this.rimLight.target)

    this.modelGroup = new THREE.Group()
    this.scene.add(this.modelGroup)
  }

  private loadModel() {
    return new Promise<void>((resolve, reject) => {
      const loader = new GLTFLoader()
      loader.setMeshoptDecoder(MeshoptDecoder)
      loader.load(
        MODEL_URL,
        (gltf) => {
          if (this.destroyed) {
            resolve()
            return
          }
          try {
            this.installModel(gltf.scene)
            resolve()
          } catch (error) {
            reject(error)
          }
        },
        (event) => {
          if (event.total > 0) {
            this.callbacks.onProgress(clamp01(event.loaded / event.total) * 0.62 + 0.04)
          }
        },
        () => reject(new Error('GEOMETRY STREAM UNAVAILABLE')),
      )
    })
  }

  private installModel(model: THREE.Object3D) {
    if (!this.scene || !this.modelGroup) return

    model.children.find((child) => child.name === '_未匹配')?.removeFromParent()

    const bounds = new THREE.Box3().setFromObject(model)
    const center = bounds.getCenter(new THREE.Vector3())
    const size = bounds.getSize(new THREE.Vector3())
    model.position.x -= center.x
    model.position.z -= center.z
    model.position.y -= bounds.min.y
    model.updateMatrixWorld(true)

    this.model = model
    this.modelRadius = Math.max(size.x, size.z, 0.2) * 0.5
    this.modelHeight = Math.max(size.y, 0.2)
    this.modelMinY = 0
    this.modelMaxY = this.modelHeight

    const graded = regradeModelMaterials(model)
    this.holo = graded.holoUniforms
    this.eyeMaterials = graded.eyeMaterials
    this.holo.uEdgeWidth.value = this.modelHeight * 0.035
    this.holo.uScanY.value = this.modelMinY - this.modelHeight

    model.traverse((item) => {
      const mesh = item as THREE.Mesh
      if (!mesh.isMesh) return
      mesh.castShadow = this.profile.shadows
      mesh.receiveShadow = false
      mesh.frustumCulled = true
    })

    this.modelGroup.add(model)
    this.modelGroup.updateWorldMatrix(true, true)

    this.collectAssemblies(model)
    this.createShowroom()
    this.createGround()
    this.createStarField()
    this.configureLighting()

    if (this.scene) {
      this.sceneFit = this.fitDistance(this.modelHeight, this.modelRadius)
      this.scene.fog = new THREE.Fog(VOID_COLOR, this.sceneFit * 1.7, this.sceneFit * 7.2)
    }

    this.callbacks.onProgress(0.72)
    this.updateCamera(0, true)
  }

  /** 收集装配树：顶升三节、随动摄像头、四组舵轮、底架 */
  private collectAssemblies(model: THREE.Object3D) {
    const assemblyRoot = model.children.find((child) => child.name.includes('监理机器人')) ?? model

    const liftRoot = assemblyRoot.children.find((child) => child.name.includes(MZZ01_MECHANISM_PROFILE.rootName))
      ?? assemblyRoot.children.find((child) => child.name.includes('顶升轴组件'))

    if (liftRoot) {
      const middleName = MZZ01_MECHANISM_PROFILE.stages[1].sourceName
      const topName = MZZ01_MECHANISM_PROFILE.stages[2].sourceName
      const middle = liftRoot.children.find((child) => child.name.includes(middleName) || child.name.includes('中间节'))
      const top = liftRoot.children.find((child) => child.name.includes(topName) || child.name.includes('顶节'))
      if (middle) {
        this.midStage = { object: middle, initialPosition: middle.position.clone() }
        MZZ01_MECHANISM_PROFILE.topStageSurfaceFollowerPatterns.forEach((pattern) => {
          middle.traverse((candidate) => {
            if (!pattern.test(candidate.name)) return
            this.topStageSurfaceFollowers.push({
              object: candidate,
              initialPosition: candidate.position.clone(),
            })
          })
        })

        MZZ01_MECHANISM_PROFILE.retractableGuides.forEach((profile) => {
          const guide = middle.getObjectByName(profile.sourceName)
          if (!guide) return

          const mesh = guide as THREE.Mesh
          const geometry = mesh.geometry
          if (!mesh.isMesh || !geometry) return
          geometry.computeBoundingBox()
          const bounds = geometry.boundingBox
          if (!bounds) return

          const axisIndex: 0 | 1 | 2 = profile.localAxis === 'x' ? 0 : profile.localAxis === 'y' ? 1 : 2
          const localAxis = new THREE.Vector3(
            axisIndex === 0 ? 1 : 0,
            axisIndex === 1 ? 1 : 0,
            axisIndex === 2 ? 1 : 0,
          )
          const initialScale = guide.scale.clone()
          const axisMin = axisIndex === 0 ? bounds.min.x : axisIndex === 1 ? bounds.min.y : bounds.min.z
          const axisMax = axisIndex === 0 ? bounds.max.x : axisIndex === 1 ? bounds.max.y : bounds.max.z
          const worldLength = (axisMax - axisMin) * Math.abs(initialScale.getComponent(axisIndex))

          this.retractableGuides.push({
            object: guide,
            initialPosition: guide.position.clone(),
            initialScale,
            initialQuaternion: guide.quaternion.clone(),
            localAxis,
            axisIndex,
            axisMin,
            worldLength,
          })
        })
      }
      if (top) this.topStage = { object: top, initialPosition: top.position.clone() }
      // 锚点挂中间节：随动位移只有顶节一半，标签不会跳跃
      const anchorStage = middle ?? top
      if (anchorStage) this.anchorNodes.set('lift', anchorStage)
    }

    assemblyRoot.children.forEach((child) => {
      const name = child.name

      // 摄像头总成（含云台桁架）：顶节随动件 + 透察锚点
      if (name.includes('摄像头')) {
        this.followers.push({ object: child, initialPosition: child.position.clone() })
        if (!this.anchorNodes.has('optics')) this.anchorNodes.set('optics', child)
        return
      }

      // 舵轮：转向演示 + 透察锚点
      if (name.includes('减震舵轮装配体')) {
        const worldCenter = new THREE.Box3().setFromObject(child).getCenter(new THREE.Vector3())
        const parent = child.parent
        const pivot = parent ? parent.worldToLocal(worldCenter.clone()) : worldCenter.clone()

        this.wheels.push({
          object: child,
          initialPosition: child.position.clone(),
          initialQuaternion: child.quaternion.clone(),
          pivot,
        })
        if (!this.anchorNodes.has('drive')) this.anchorNodes.set('drive', child)
        return
      }

      if (name === '底架') {
        this.anchorNodes.set('chassis', child)
      }
    })
  }

  /**
   * 暗色展厅：紧凑设备舱、三块背墙金属板与低亮顶缝。
   * 房间尺寸由取景距离推导，保证所有机位都在厅内。
   */
  private createShowroom() {
    if (!this.scene) return

    const r = this.modelRadius
    const h = this.modelHeight
    const fit = this.fitDistance(h * 1.45, r)
    // 从原展厅再收紧约 16%，让建筑空间服务机器人而非读成一条科幻通道。
    const halfW = Math.max(fit * 1.2, r * 3.9)
    const backZ = -Math.max(fit * 0.98, r * 3.05)
    const frontZ = Math.max(fit * 1.28, r * 4.05)
    const ceilY = h * 2.08
    const depth = frontZ - backZ
    const wallThickness = Math.max(r * 0.07, 0.08)
    this.showroomHalfWidth = halfW
    this.showroomBackZ = backZ
    this.showroomFrontZ = frontZ

    const wallMaterial = new THREE.MeshStandardMaterial({
      color: 0x0C1117,
      roughness: 0.84,
      metalness: 0.08,
      envMapIntensity: 0.04,
    })
    this.wallMaterial = wallMaterial

    // 三块背墙板保留真实厚度和两条结构缝，不把墙做成一张没有尺度感的平面。
    const panelCount = 3
    const panelGap = Math.max(r * 0.026, 0.026)
    const panelWidth = (halfW * 2 - panelGap * (panelCount - 1)) / panelCount
    for (let index = 0; index < panelCount; index += 1) {
      const panel = new THREE.Mesh(new THREE.BoxGeometry(panelWidth, ceilY, wallThickness), wallMaterial)
      panel.position.set(
        -halfW + panelWidth / 2 + index * (panelWidth + panelGap),
        ceilY / 2,
        backZ - wallThickness / 2,
      )
      this.scene.add(panel)
    }

    const leftWall = new THREE.Mesh(new THREE.BoxGeometry(wallThickness, ceilY, depth), wallMaterial)
    leftWall.position.set(-halfW - wallThickness / 2, ceilY / 2, backZ + depth / 2)
    this.scene.add(leftWall)

    const rightWall = new THREE.Mesh(new THREE.BoxGeometry(wallThickness, ceilY, depth), wallMaterial)
    rightWall.position.set(halfW + wallThickness / 2, ceilY / 2, backZ + depth / 2)
    this.scene.add(rightWall)

    const ceiling = new THREE.Mesh(new THREE.BoxGeometry(halfW * 2, wallThickness, depth), wallMaterial)
    ceiling.position.set(0, ceilY + wallThickness / 2, backZ + depth / 2)
    this.scene.add(ceiling)

    // 低亮顶缝只提供空间纵深，不再与机器人争夺主体亮度。
    const stripMaterial = new THREE.MeshBasicMaterial({
      color: new THREE.Color(1.05, 1.13, 1.18),
      toneMapped: false,
      transparent: true,
      opacity: 0,
    })
    this.stripMaterial = stripMaterial

    const ceilStripGeometry = new THREE.BoxGeometry(r * 0.045, r * 0.016, depth * 0.64)
    const stripCount = 3
    for (let index = 0; index < stripCount; index += 1) {
      const spread = (index / (stripCount - 1) - 0.5) * 2
      const strip = new THREE.Mesh(ceilStripGeometry, stripMaterial)
      strip.position.set(spread * halfW * 0.52, ceilY - r * 0.016, backZ + depth * 0.5)
      this.scene!.add(strip)
    }

    // 只留一盏弱背墙掠射光：用于读出板材与字的材质，不承担机器人主照明。
    this.wallGrazingLight = new THREE.SpotLight(0xE5EDF2, 0, ceilY * 2, 0.42, 0.95, 2)
    this.wallGrazingLight.position.set(-halfW * 0.62, h * 1.34, backZ + r * 0.72)
    this.wallGrazingLight.target.position.set(halfW * 0.16, h * 0.94, backZ)
    this.scene.add(this.wallGrazingLight, this.wallGrazingLight.target)

    // 背墙身份牌：哑光金属文字以弱掠射光读形，叠加层只模拟极弱的冷白表面反射。
    const textCanvas = document.createElement('canvas')
    textCanvas.width = 2048
    textCanvas.height = 512
    const drawWallText = () => {
      const ctx = textCanvas.getContext('2d')
      if (!ctx) return
      ctx.clearRect(0, 0, 2048, 512)
      ctx.fillStyle = '#FFFFFF'
      ctx.font = '500 278px "Instrument Sans", "HarmonyOS Sans SC", sans-serif'
      ctx.textAlign = 'center'
      ctx.textBaseline = 'middle'
      ctx.fillText('HENGVIEW', 1024, 276)
    }
    drawWallText()
    const textTexture = new THREE.CanvasTexture(textCanvas)
    textTexture.colorSpace = THREE.SRGBColorSpace
    textTexture.anisotropy = 4
    if (typeof document.fonts?.ready?.then === 'function') {
      void document.fonts.ready.then(() => {
        drawWallText()
        textTexture.needsUpdate = true
      })
    }
    const textWidth = halfW * 0.78
    this.wallTextMaterial = new THREE.MeshStandardMaterial({
      color: THEME_WALL_TEXT_DARK,
      map: textTexture,
      transparent: true,
      opacity: 0.46,
      roughness: 0.64,
      metalness: 0.42,
      envMapIntensity: 0.14,
      depthWrite: false,
    })
    const wallText = new THREE.Mesh(new THREE.PlaneGeometry(textWidth, textWidth / 4), this.wallTextMaterial)
    wallText.position.set(0, h * 1.0, backZ + 0.012)
    wallText.renderOrder = 1
    this.scene.add(wallText)

    // 光泽层与金属字共用 alpha 遮罩；NormalBlending 使它保持为材质反射而非霓虹发光。
    this.wallTextSheenMaterial = new THREE.ShaderMaterial({
      vertexShader: WALL_TEXT_SHEEN_VERTEX,
      fragmentShader: WALL_TEXT_SHEEN_FRAGMENT,
      uniforms: {
        uMap: { value: textTexture },
        uTime: { value: 0 },
        uOpacity: { value: 0 },
      },
      transparent: true,
      depthWrite: false,
      toneMapped: false,
      blending: THREE.NormalBlending,
    })
    const wallTextSheen = new THREE.Mesh(
      new THREE.PlaneGeometry(textWidth, textWidth / 4),
      this.wallTextSheenMaterial,
    )
    wallTextSheen.position.set(0, h * 1.0, backZ + 0.016)
    wallTextSheen.renderOrder = 2
    this.scene.add(wallTextSheen)
  }

  private createGround() {
    if (!this.scene) return

    const radius = this.modelRadius

    // 主地板与展厅墙体共用同一组宽深边界，避免低机位拍到有限圆形地板之外的黑色虚空。
    const floorWidth = this.showroomHalfWidth * 2
    const floorDepth = this.showroomFrontZ - this.showroomBackZ
    const floorCenterZ = this.showroomBackZ + floorDepth * 0.5
    this.floorBaseMaterial = new THREE.MeshBasicMaterial({ color: THEME_FLOOR_DARK })
    const base = new THREE.Mesh(
      new THREE.PlaneGeometry(floorWidth, floorDepth),
      this.floorBaseMaterial,
    )
    base.rotation.x = -Math.PI / 2
    base.position.set(0, -0.002, floorCenterZ)
    this.scene.add(base)

    if (this.profile.shadows) {
      this.catcherMaterial = new THREE.ShadowMaterial({ opacity: 0.34 })
      const catcher = new THREE.Mesh(new THREE.CircleGeometry(radius * 3.6, 64), this.catcherMaterial)
      catcher.rotation.x = -Math.PI / 2
      catcher.position.y = 0.0005
      catcher.receiveShadow = true
      this.scene.add(catcher)
    }

    // 接触压暗：脚下柔和吸光渐变
    const contactCanvas = document.createElement('canvas')
    contactCanvas.width = contactCanvas.height = 128
    const ctx = contactCanvas.getContext('2d')
    if (ctx) {
      const gradient = ctx.createRadialGradient(64, 64, 6, 64, 64, 64)
      gradient.addColorStop(0, 'rgba(0,0,0,0.62)')
      gradient.addColorStop(0.55, 'rgba(0,0,0,0.3)')
      gradient.addColorStop(1, 'rgba(0,0,0,0)')
      ctx.fillStyle = gradient
      ctx.fillRect(0, 0, 128, 128)
    }
    const contactTexture = new THREE.CanvasTexture(contactCanvas)
    this.contactMaterial = new THREE.MeshBasicMaterial({
      map: contactTexture,
      transparent: true,
      depthWrite: false,
    })
    const contact = new THREE.Mesh(new THREE.CircleGeometry(radius * 1.55, 48), this.contactMaterial)
    contact.rotation.x = -Math.PI / 2
    contact.position.y = 0.001
    this.scene.add(contact)

    // 标线层：透明叠加
    this.groundMaterial = new THREE.ShaderMaterial({
      vertexShader: GROUND_VERTEX,
      fragmentShader: GROUND_FRAGMENT,
      uniforms: THREE.UniformsUtils.merge([
        THREE.UniformsLib.fog,
        {
          uSteel: { value: new THREE.Color(0x53687A) },
          uCyan: { value: new THREE.Color(0x46D7EA) },
          uR: { value: radius },
          uTime: { value: 0 },
          uMarkFade: { value: 0.0 },
          uBootRingRadius: { value: 0.0 },
          uBootRingAlpha: { value: 0.0 },
        },
      ]),
      fog: true,
      transparent: true,
      depthWrite: false,
    })
    const marks = new THREE.Mesh(new THREE.CircleGeometry(radius * 9.5, 96), this.groundMaterial)
    marks.rotation.x = -Math.PI / 2
    marks.position.y = 0.004
    this.scene.add(marks)
  }

  /** 流动星光场 + 两盏轨道点光（金属上的流动高光来源） */
  private createStarField() {
    if (!this.scene) return

    const r = this.modelRadius
    const h = this.modelHeight
    const count = this.profile.compact ? 110 : 230

    const radii = new Float32Array(count)
    const baseY = new Float32Array(count)
    const phases = new Float32Array(count)
    const speeds = new Float32Array(count)
    const sizes = new Float32Array(count)
    const hues = new Float32Array(count)

    for (let i = 0; i < count; i += 1) {
      const t = Math.random()
      radii[i] = r * (0.9 + Math.sqrt(t) * 2.9)
      baseY[i] = h * (0.05 + Math.pow(Math.random(), 1.5) * 1.9)
      phases[i] = Math.random() * Math.PI * 2
      speeds[i] = (0.02 + Math.random() * 0.05) * (Math.random() < 0.45 ? -1 : 1)
      const bright = Math.random() < 0.14
      sizes[i] = bright ? 1.7 + Math.random() * 0.8 : 0.7 + Math.random() * 0.7
      hues[i] = bright ? 0.75 + Math.random() * 0.25 : Math.random() * 0.35
    }

    // 实例化条痕：单位四边形 + 每星参数，方向与长度在顶点着色器按屏幕速度求得
    const plane = new THREE.PlaneGeometry(1, 1)
    const geometry = new THREE.InstancedBufferGeometry()
    geometry.index = plane.index
    geometry.setAttribute('position', plane.getAttribute('position'))
    geometry.instanceCount = count
    geometry.setAttribute('aRadius', new THREE.InstancedBufferAttribute(radii, 1))
    geometry.setAttribute('aBaseY', new THREE.InstancedBufferAttribute(baseY, 1))
    geometry.setAttribute('aPhase', new THREE.InstancedBufferAttribute(phases, 1))
    geometry.setAttribute('aSpeed', new THREE.InstancedBufferAttribute(speeds, 1))
    geometry.setAttribute('aSize', new THREE.InstancedBufferAttribute(sizes, 1))
    geometry.setAttribute('aHue', new THREE.InstancedBufferAttribute(hues, 1))

    this.starMaterial = new THREE.ShaderMaterial({
      vertexShader: STAR_VERTEX,
      fragmentShader: STAR_FRAGMENT,
      uniforms: {
        uTime: { value: 0 },
        uTrail: { value: this.profile.compact ? 0.45 : 0.6 },
        uViewport: { value: new THREE.Vector2(1, 1) },
        uPixelRatio: { value: this.profile.basePixelRatio },
        uScale: { value: this.fitDistance(h, r) * 2.6 },
        uOpacity: { value: 0 },
      },
      transparent: true,
      depthWrite: false,
      depthTest: true,
      side: THREE.DoubleSide,
      blending: THREE.AdditiveBlending,
    })

    const streaks = new THREE.Mesh(geometry, this.starMaterial)
    streaks.frustumCulled = false
    this.scene.add(streaks)
    this.syncStarViewport()

    // 轨道点光改为中性冷白，只塑造金属高光，不再把青色扩散为环境装饰。
    this.orbitLightA = new THREE.PointLight(0xB7C8D3, 0, r * 7, 2)
    this.orbitLightB = new THREE.PointLight(0xD8E9F4, 0, r * 8, 2)
    this.scene.add(this.orbitLightA, this.orbitLightB)
  }

  /** 条痕粒子的像素空间换算依赖真实绘制缓冲尺寸 */
  private syncStarViewport() {
    if (!this.renderer || !this.starMaterial) return
    const size = this.renderer.getDrawingBufferSize(new THREE.Vector2())
    ;(this.starMaterial.uniforms.uViewport!.value as THREE.Vector2).copy(size)
  }

  private configureLighting() {
    if (!this.keyLight || !this.rimLight) return

    const r = this.modelRadius
    const h = this.modelHeight

    this.keyLight.position.set(r * 2.05, h * 2.65, r * 2.45)
    this.keyLight.target.position.set(0, h * 0.42, 0)
    if (this.profile.shadows) {
      this.keyLight.castShadow = true
      this.keyLight.shadow.mapSize.set(1024, 1024)
      this.keyLight.shadow.bias = -0.0003
      this.keyLight.shadow.normalBias = 0.02
      this.keyLight.shadow.radius = 4
      const range = Math.max(r * 2.4, h * 1.4)
      this.keyLight.shadow.camera.near = 0.1
      this.keyLight.shadow.camera.far = range * 6
      this.keyLight.shadow.camera.updateProjectionMatrix()
    }

    this.rimLight.position.set(-r * 2.5, h * 2.2, -r * 2.3)
    this.rimLight.target.position.set(0, h * 0.55, 0)
  }

  private async prewarm() {
    if (!this.renderer || !this.scene || !this.camera) return

    this.post = new HengviewPost(this.renderer, this.scene, this.camera, {
      lean: this.profile.compact,
      multisampling: this.profile.multisampling,
    })
    this.resize()

    // 开机前状态：全息强度 0、扫描面在地面之下 → 机体是吞光剪影
    this.setLightScale(this.profile.reducedMotion ? 1 : 0.32)
    this.callbacks.onProgress(0.86)

    try {
      await this.renderer.compileAsync(this.scene, this.camera)
    } catch {
      this.renderer.compile(this.scene, this.camera)
    }
    this.callbacks.onProgress(0.95)

    // 遮罩仍覆盖时强制首两帧渲染，完成材质/阴影贴图编译
    this.post.render(0.016)
    this.post.render(0.016)
  }

  /** 记录开机亮度系数；实际数值由 applyLighting 按主题每帧合成 */
  private setLightScale(scale: number) {
    this.lightScale = scale
    this.applyLighting()
  }

  /**
   * 主题化光照合成：暗厅 ↔ 白厅 的全部差异集中在这一张查表里，
   * 由 themeMixEased（0..1）与开机系数 lightScale 每帧插值。
   */
  private applyLighting() {
    const scale = this.lightScale
    const m = this.themeMixEased
    const L = (dark: number, light: number) => THREE.MathUtils.lerp(dark, light, m)

    if (this.keyLight) this.keyLight.intensity = L(KEY_INTENSITY, 42) * scale
    if (this.rimLight) this.rimLight.intensity = L(RIM_INTENSITY, 1.05) * scale
    if (this.hemiLight) {
      this.hemiLight.intensity = L(HEMI_INTENSITY, 1.5) * (0.4 + scale * 0.6)
      this.hemiLight.color.lerpColors(THEME_HEMI_SKY_DARK, THEME_HEMI_SKY_LIGHT, m)
      this.hemiLight.groundColor.lerpColors(THEME_HEMI_GROUND_DARK, THEME_HEMI_GROUND_LIGHT, m)
    }
    if (this.scene) this.scene.environmentIntensity = L(ENV_INTENSITY, 0.8) * (0.25 + scale * 0.75)
    if (this.groundMaterial) {
      this.groundMaterial.uniforms.uMarkFade!.value = (0.3 + scale * 0.7) * L(1, 0.85)
      ;(this.groundMaterial.uniforms.uSteel!.value as THREE.Color)
        .lerpColors(THEME_STEEL_DARK, THEME_STEEL_LIGHT, m)
      ;(this.groundMaterial.uniforms.uCyan!.value as THREE.Color)
        .lerpColors(THEME_CYAN_DARK, THEME_CYAN_LIGHT, m)
    }
    if (this.orbitLightA) this.orbitLightA.intensity = (this.profile.compact ? 1.5 : 2.1) * scale * (1 - m * 0.8)
    if (this.orbitLightB) this.orbitLightB.intensity = (this.profile.compact ? 1.1 : 1.6) * scale * (1 - m * 0.8)
    if (this.starMaterial) this.starMaterial.uniforms.uOpacity!.value = scale * 0.62 * (1 - m * 0.85)

    const pulse = 1 + this.stripPulse * 0.22
    if (this.stripMaterial) {
      this.stripMaterial.opacity = (0.04 + scale * 0.32) * pulse
      this.stripMaterial.color.lerpColors(THEME_STRIP_DARK, THEME_STRIP_LIGHT, m).multiplyScalar(pulse)
    }
    if (this.wallGrazingLight) {
      this.wallGrazingLight.intensity = L(this.profile.compact ? 0.9 : 1.35, 0.18) * scale * (1 + this.bloomBoost * 0.32)
    }

    if (this.scene) {
      const voidColor = this.scene.background as THREE.Color
      voidColor.lerpColors(THEME_VOID_DARK, THEME_VOID_LIGHT, m)
      this.renderer?.setClearColor(voidColor, 1)
    }
    if (this.wallMaterial) this.wallMaterial.color.lerpColors(THEME_WALL_DARK, THEME_WALL_LIGHT, m)
    if (this.floorBaseMaterial) this.floorBaseMaterial.color.lerpColors(THEME_FLOOR_DARK, THEME_FLOOR_LIGHT, m)
    if (this.catcherMaterial) this.catcherMaterial.opacity = L(0.34, 0.08) // 白厅大幅削弱阴影
    if (this.contactMaterial) this.contactMaterial.opacity = L(1, 0.18) // 白厅几乎不可见接触压暗
    if (this.wallTextMaterial) {
      this.wallTextMaterial.color.lerpColors(THEME_WALL_TEXT_DARK, THEME_WALL_TEXT_LIGHT, m)
      this.wallTextMaterial.opacity = L(0.46, 0.52) * (0.35 + scale * 0.65)
    }
    if (this.wallTextSheenMaterial) {
      // 白厅必须退场，让身份牌继续由真实掠射光而非效果层塑形。
      const darkSheen = 1 - smoothstep(0.28, 0.78, m)
      this.wallTextSheenMaterial.uniforms.uOpacity!.value = (this.profile.compact ? 0.048 : 0.068) * scale * darkSheen
    }
    if (this.holo) this.holo.uHoloColor.value.lerpColors(THEME_CYAN_DARK, THEME_CYAN_LIGHT, m)
    if (this.scene?.fog instanceof THREE.Fog) {
      this.scene.fog.color.lerpColors(THEME_VOID_DARK, THEME_VOID_LIGHT, m)
      this.scene.fog.near = this.sceneFit * L(1.7, 2.6)
      this.scene.fog.far = this.sceneFit * L(7.2, 18)
    }
    this.baseExposure = L(BASE_EXPOSURE, 1.16)
    this.post?.applyTheme(m)
  }

  /** 日夜切换：白厅 = 开灯事件（灯带脉冲 → 全场光照过渡） */
  setTheme(mode: 'dark' | 'light') {
    const target = mode === 'light' ? 1 : 0
    if (target === this.themeTarget) return
    this.themeTarget = target
    this.stripPulse = this.profile.reducedMotion ? 0 : 1
    if (this.profile.reducedMotion) {
      this.themeMix = target
      this.themeMixEased = target
    }
  }

  private completeBoot(instant = false) {
    if (!this.holo) return

    this.bootScanProgress = 1
    this.holo.uScanY.value = this.modelMaxY + this.modelHeight
    this.holo.uHoloIntensity.value = 0
    this.setLightScale(1)
    if (this.groundMaterial) {
      this.groundMaterial.uniforms.uBootRingAlpha!.value = 0
    }
    if (!instant) {
      this.bloomBoost = 1
      this.exposureBoost = 0.24
    }
    this.setPhase('live')
  }

  // ---------------------------------------------------------------- 渲染循环

  private startAnimation() {
    if (this.destroyed || this.animationFrame !== null) return
    this.animationFrame = window.requestAnimationFrame(this.render)
  }

  private stopAnimation() {
    if (this.animationFrame === null) return
    window.cancelAnimationFrame(this.animationFrame)
    this.animationFrame = null
  }

  private handleVisibilityChange = () => {
    if (document.visibilityState === 'visible') {
      this.lastFrameTime = 0
      this.startAnimation()
    } else {
      this.stopAnimation()
    }
  }

  private render = (time: number) => {
    this.animationFrame = null
    if (this.destroyed || !this.renderer || !this.scene || !this.camera || !this.post) return

    const delta = this.lastFrameTime ? Math.min((time - this.lastFrameTime) / 1000, 0.05) : 0.016
    this.lastFrameTime = time
    this.elapsed += delta

    this.updateFrame(delta)
    this.post.render(delta)
    this.trackQuality(delta)

    if (document.visibilityState === 'visible') this.startAnimation()
  }

  private updateFrame(delta: number) {
    if (!this.holo || !this.renderer) return

    this.holo.uHoloTime.value = this.elapsed
    if (this.groundMaterial) this.groundMaterial.uniforms.uTime!.value = this.elapsed
    if (this.starMaterial) this.starMaterial.uniforms.uTime!.value = this.elapsed
    if (this.wallTextSheenMaterial) {
      this.wallTextSheenMaterial.uniforms.uTime!.value = this.profile.reducedMotion ? 0 : this.elapsed
    }

    // 日夜过渡：主题混合值缓动逼近目标 + 灯带脉冲衰减，每帧合成光照
    this.themeMix += (this.themeTarget - this.themeMix) * (1 - Math.exp(-delta * 3.2))
    this.themeMixEased = easeInOutCubic(this.themeMix)
    this.stripPulse = Math.max(0, this.stripPulse - delta * 1.9)
    this.applyLighting()

    // 轨道点光：两条错相椭圆轨道，在机体金属上留下缓慢流动的高光
    const orbitR = this.modelRadius
    const orbitH = this.modelHeight
    if (this.orbitLightA) {
      const a = this.elapsed * 0.14 + 1.2
      this.orbitLightA.position.set(
        Math.cos(a) * orbitR * 1.8,
        orbitH * (0.62 + Math.sin(this.elapsed * 0.31) * 0.12),
        Math.sin(a) * orbitR * 1.8,
      )
    }
    if (this.orbitLightB) {
      const b = -this.elapsed * 0.09 + 3.9
      this.orbitLightB.position.set(
        Math.cos(b) * orbitR * 2.4,
        orbitH * (1.05 + Math.sin(this.elapsed * 0.23 + 1.0) * 0.14),
        Math.sin(b) * orbitR * 2.4,
      )
    }

    // 滚动阻尼
    const damping = 1 - Math.exp(-delta * 8)
    this.scroll += (this.scrollTarget - this.scroll) * damping

    if (this.phase === 'boot') this.updateBoot()

    const s = this.phase === 'live' && !this.profile.reducedMotion ? this.scroll : (this.phase === 'live' ? this.scroll : 0)

    // —— 机构姿态：原始 GLB 为最高点；SEC 02 先向内收拢，再回到最高工作位。舵轮转向同样是滚动的可逆纯函数。
    const retractT = this.profile.reducedMotion
      ? 0
      : smoothstep(0.38, 0.52, s) * (1 - smoothstep(0.56, 0.635, s))
    const steerWindow = clamp01((s - 0.635) / 0.13)
    const steerT = this.profile.reducedMotion ? 0 : Math.sin(steerWindow * Math.PI)

    this.applyPose(retractT, steerT)

    // —— 断层透察（SEC 04）：整机不拆散，一条实体切片带自下而上扫描，
    //    带外转为 X-ray 全息 —— 结构以"透视"而非"散架"的方式被阅读
    const sliceGhost = this.profile.reducedMotion
      ? 0
      : smoothstep(0.745, 0.785, s) * (1 - smoothstep(0.925, 0.958, s))
    const sliceTravel = smoothstep(0.77, 0.925, s)
    if (this.holo) {
      const effHeight = this.modelHeight - MZZ01_MECHANISM_PROFILE.stages[2].travel * retractT
      const bandHeight = effHeight * 0.24
      const center = -bandHeight * 0.6 + (effHeight + bandHeight * 1.2) * sliceTravel
      this.holo.uSliceGhost.value = sliceGhost
      this.holo.uSliceLow.value = center - bandHeight * 0.5
      this.holo.uSliceHigh.value = center + bandHeight * 0.5
    }
    this.sliceGhost = sliceGhost
    this.sliceTravel = sliceTravel

    // —— 眼睛（镜头玻璃）呼吸
    const online = this.phase === 'live'
    const eyeBase = online ? 1.25 : 0
    const eyeBreath = online && !this.profile.reducedMotion ? Math.sin(this.elapsed * 1.6) * 0.3 : 0
    const eyeIntensity = Math.max(0, eyeBase + eyeBreath + this.bloomBoost * 1.2)
    this.eyeMaterials.forEach((material) => {
      material.emissiveIntensity = eyeIntensity
    })

    // —— 脉冲衰减（开机完成的一次性高光）
    this.bloomBoost = Math.max(0, this.bloomBoost - delta * 2.4)
    this.exposureBoost = Math.max(0, this.exposureBoost - delta * 0.9)
    this.post?.setBloomBoost(this.bloomBoost)
    this.renderer.toneMappingExposure = this.baseExposure * (1 + this.exposureBoost)

    this.updateCamera(delta, false, retractT)
    this.emitTelemetry(s, retractT)
    this.emitAnchors(s)
  }

  private updateBoot() {
    if (!this.holo || this.bootStartedAt === null) return

    const t = (performance.now() / 1000 - this.bootStartedAt) / this.bootTimeScale

    // 阶段 A：全息鬼影浮现（0 – 0.9s）
    const ghost = easeOutCubic(t / 0.9)
    this.holo.uHoloIntensity.value = ghost

    // 阶段 B：扫描物化（0.9 – 3.0s）
    const scan = easeInOutCubic((t - 0.9) / 2.1)
    this.bootScanProgress = scan
    const span = this.modelMaxY - this.modelMinY + this.holo.uEdgeWidth.value * 4
    this.holo.uScanY.value = this.modelMinY - this.holo.uEdgeWidth.value * 2 + span * scan

    this.setLightScale(0.32 + scan * 0.68)

    if (this.groundMaterial) {
      this.groundMaterial.uniforms.uBootRingRadius!.value = scan * this.modelRadius * 3.4
      this.groundMaterial.uniforms.uBootRingAlpha!.value = Math.sin(clamp01(scan) * Math.PI) * 0.8
    }

    // 阶段 C：完成 + 开机脉冲
    if (t >= 3.05) this.completeBoot()
  }

  /** 机构姿态应用：原始模型是最高工作位；先复位，再按收拢量向内套叠 / 转向 */
  private applyPose(retractT: number, steerT: number) {
    // 静止段完全跳过（离开活动段后再复位一帧）
    const active = retractT > 0.0001 || steerT > 0.0001
    if (!active && !this.lastPoseActive) return
    this.lastPoseActive = active

    const midTravel = MZZ01_MECHANISM_PROFILE.stages[1].travel
    const topTravel = MZZ01_MECHANISM_PROFILE.stages[2].travel

    if (this.midStage) {
      this.midStage.object.position.copy(this.midStage.initialPosition)
      this.midStage.object.position.y -= midTravel * retractT
    }

    let followerOffset: THREE.Vector3 | null = null
    if (this.topStage) {
      this.topStage.object.position.copy(this.topStage.initialPosition)
      this.topStage.object.position.y -= topTravel * retractT
      followerOffset = this.getWorldOffsetForLocalY(this.topStage.object, -topTravel * retractT)
    }

    const relativeTopTravel = Math.max(0, topTravel - midTravel) * retractT
    this.applyRetractableGuides(relativeTopTravel)

    const surfaceFollowerOffset = this.topStage
      ? this.getWorldOffsetForLocalY(this.topStage.object, -relativeTopTravel)
      : null
    this.topStageSurfaceFollowers.forEach((entry) => {
      entry.object.position.copy(entry.initialPosition)
      if (surfaceFollowerOffset) this.applyWorldOffset(entry.object, surfaceFollowerOffset)
    })

    this.followers.forEach((entry) => {
      entry.object.position.copy(entry.initialPosition)
      if (followerOffset) this.applyWorldOffset(entry.object, followerOffset)
    })

    this.wheels.forEach((entry) => {
      entry.object.position.copy(entry.initialPosition)
      entry.object.quaternion.copy(entry.initialQuaternion)

      // 全向转向演示：绕自身竖轴同步偏转
      if (steerT > 0.001) {
        const angle = 0.42 * steerT
        this.tempQuat.setFromAxisAngle(this.tempVecA.set(0, 1, 0), angle)
        this.tempVecB.copy(entry.initialPosition).sub(entry.pivot).applyQuaternion(this.tempQuat).add(entry.pivot)
        entry.object.position.copy(this.tempVecB)
        entry.object.quaternion.premultiply(this.tempQuat)
      }
    })
  }

  /**
   * 中间节内的两根长导杆由顶节遮蔽。顶节相对中间节收回多少，导杆外露段就缩短多少。
   * 沿网格局部长轴缩放，并补偿局部位置，使导杆下端固定、上端向箱体内部收回。
   */
  private applyRetractableGuides(hiddenLength: number) {
    this.retractableGuides.forEach((entry) => {
      entry.object.position.copy(entry.initialPosition)
      entry.object.scale.copy(entry.initialScale)
      entry.object.quaternion.copy(entry.initialQuaternion)

      const visibleLength = Math.max(0, entry.worldLength - hiddenLength)
      const visibleRatio = entry.worldLength > 0
        ? Math.max(0.001, visibleLength / entry.worldLength)
        : 1
      const initialAxisScale = entry.initialScale.getComponent(entry.axisIndex)
      entry.object.scale.setComponent(
        entry.axisIndex,
        initialAxisScale * visibleRatio,
      )

      const anchorCompensation = entry.localAxis
        .clone()
        .applyQuaternion(entry.initialQuaternion)
        .multiplyScalar(entry.axisMin * initialAxisScale * (1 - visibleRatio))
      entry.object.position.add(anchorCompensation)
    })
  }

  private getWorldOffsetForLocalY(object: THREE.Object3D, distance: number) {
    const parent = object.parent
    if (!parent) return new THREE.Vector3(0, distance, 0)
    parent.updateWorldMatrix(true, false)
    const origin = parent.localToWorld(new THREE.Vector3())
    const target = parent.localToWorld(new THREE.Vector3(0, distance, 0))
    return target.sub(origin)
  }

  private applyWorldOffset(object: THREE.Object3D, worldOffset: THREE.Vector3) {
    const parent = object.parent
    if (!parent) {
      object.position.add(worldOffset)
      return
    }
    parent.updateWorldMatrix(true, false)
    const parentOrigin = parent.localToWorld(this.tempVecA.set(0, 0, 0)).clone()
    const localOrigin = parent.worldToLocal(parentOrigin.clone())
    const localDestination = parent.worldToLocal(parentOrigin.add(worldOffset))
    object.position.add(localDestination.sub(localOrigin))
  }

  // ---------------------------------------------------------------- 相机

  private fitDistance(height: number, radius: number) {
    if (!this.camera) return 10
    const vFov = THREE.MathUtils.degToRad(this.camera.fov)
    const hFov = 2 * Math.atan(Math.tan(vFov / 2) * this.camera.aspect)
    return Math.max(
      (height / (2 * Math.tan(vFov / 2))) * 1.16,
      (radius / Math.tan(hFov / 2)) * 1.24,
    )
  }

  private updateCamera(delta: number, immediate: boolean, retractT = 0) {
    if (!this.camera || !this.model) return

    const topTravel = MZZ01_MECHANISM_PROFILE.stages[2].travel
    const effHeight = this.modelHeight - topTravel * retractT
    const effRadius = this.modelRadius

    let yaw: number
    let k: number
    let camH: number
    let focH: number

    if (this.phase !== 'live') {
      // 开机机位：从低角度缓推至首屏英雄位
      const e = easeInOutCubic(this.bootScanProgress)
      const first = CAMERA_KEYS[0]!
      yaw = THREE.MathUtils.lerp(16, first.yaw, e)
      k = THREE.MathUtils.lerp(1.62, first.k, e)
      camH = THREE.MathUtils.lerp(0.16, first.camH, e)
      focH = THREE.MathUtils.lerp(0.3, first.focH, e)
    } else {
      const s = this.profile.reducedMotion ? 0 : this.scroll
      let upper = CAMERA_KEYS.length - 1
      for (let index = 1; index < CAMERA_KEYS.length; index += 1) {
        if (CAMERA_KEYS[index]!.at >= s) {
          upper = index
          break
        }
      }
      const b = CAMERA_KEYS[upper]!
      const a = CAMERA_KEYS[upper - 1] ?? b
      const t = b.at === a.at ? 1 : smoothstep(a.at, b.at, s)
      yaw = THREE.MathUtils.lerp(a.yaw, b.yaw, t)
      k = THREE.MathUtils.lerp(a.k, b.k, t)
      camH = THREE.MathUtils.lerp(a.camH, b.camH, t)
      focH = THREE.MathUtils.lerp(a.focH, b.focH, t)

      // 待机 / 收束段的呼吸性微漂移
      if (!this.profile.reducedMotion && (s < 0.1 || s > 0.96)) {
        yaw += Math.sin(this.elapsed * 0.32) * 1.2
        camH += Math.sin(this.elapsed * 0.21) * 0.006
      }

      // 断层透察时：机位与焦点跟随切片带缓慢上移，镜头是"检测探头"
      if (this.sliceGhost > 0.001) {
        const bandRatio = -0.12 + 1.18 * this.sliceTravel
        focH = THREE.MathUtils.lerp(focH, THREE.MathUtils.clamp(bandRatio, 0.08, 0.96), this.sliceGhost)
        camH = THREE.MathUtils.lerp(camH, THREE.MathUtils.clamp(bandRatio + 0.05, 0.12, 1.0), this.sliceGhost)
        k = THREE.MathUtils.lerp(k, 0.86, this.sliceGhost)
      }
    }

    const dist = this.fitDistance(effHeight, effRadius) * k
    const yawRad = THREE.MathUtils.degToRad(yaw)

    this.cameraDesired.set(
      Math.sin(yawRad) * dist,
      Math.max(camH * effHeight, this.modelHeight * 0.04),
      Math.cos(yawRad) * dist,
    )
    this.cameraFocusDesired.set(0, focH * effHeight, 0)

    if (immediate) {
      this.cameraPosition.copy(this.cameraDesired)
      this.cameraFocus.copy(this.cameraFocusDesired)
    } else {
      const rate = 1 - Math.exp(-delta * 6.4)
      this.cameraPosition.lerp(this.cameraDesired, rate)
      this.cameraFocus.lerp(this.cameraFocusDesired, rate)
    }

    this.camera.position.copy(this.cameraPosition)
    this.camera.lookAt(this.cameraFocus)
  }

  // ---------------------------------------------------------------- 遥测与锚点

  private sectionIndex(s: number) {
    let section = 0
    HENGVIEW_SECTIONS.forEach((edge, index) => {
      if (s >= edge) section = index + 1
    })
    return section
  }

  private emitTelemetry(s: number, retractT: number) {
    if (this.elapsed - this.telemetryAt < 0.1) return
    this.telemetryAt = this.elapsed

    this.callbacks.onTelemetry({
      section: this.sectionIndex(s),
      scanPct: Math.round(this.bootScanProgress * 100),
      liftMeters: (1 - retractT) * LIFT_RANGE_METERS,
      explodePct: Math.round(this.sliceTravel * this.sliceGhost * 100),
      online: this.phase === 'live',
    })
  }

  private emitAnchors(s: number) {
    if (!this.camera) return

    const scanning = this.sliceGhost > 0.3
    const shouldEmit = scanning !== this.lastAnchorsVisible || this.elapsed - this.anchorsAt > 1 / 24
    if (!shouldEmit) return
    this.anchorsAt = this.elapsed
    this.lastAnchorsVisible = scanning

    const anchors: HengviewAnchor[] = []
    const pushAnchor = (id: string, code: string, label: string, visible: boolean) => {
      const node = this.anchorNodes.get(id)
      if (!node) return
      new THREE.Box3().setFromObject(node).getCenter(this.tempVecA)
      this.tempVecA.project(this.camera!)
      anchors.push({
        id,
        code,
        label,
        x: THREE.MathUtils.clamp((this.tempVecA.x + 1) * 50, -10, 110),
        y: THREE.MathUtils.clamp((1 - this.tempVecA.y) * 50, -10, 110),
        visible: visible && this.tempVecA.z > -1 && this.tempVecA.z < 1,
      })
    }

    const inWindow = (from: number, to: number) => s >= from && s <= to && this.phase === 'live'

    pushAnchor('optics', 'A-01', '双目云台', inWindow(0.16, 0.32) && !this.profile.reducedMotion)
    pushAnchor('lift', 'A-02', '三级顶升', inWindow(0.37, 0.6) && !this.profile.reducedMotion)
    pushAnchor('drive', 'A-03', '减震舵轮', inWindow(0.65, 0.75) && !this.profile.reducedMotion)

    // 断层透察：切片带扫过哪一站，哪一站的检定标签亮起（同一时刻只有一个焦点）
    const travel = this.sliceTravel
    pushAnchor('drive', 'B-01', '减震舵轮 ×4 ', scanning && travel < 0.24)
    pushAnchor('chassis', 'B-02', '底架总成 ', scanning && travel >= 0.24 && travel < 0.4)
    pushAnchor('lift', 'B-03', '顶升轴组件 ', scanning && travel >= 0.4 && travel < 0.78)
    pushAnchor('optics', 'B-04', '双目云台 ×2 ', scanning && travel >= 0.78)
 
    this.callbacks.onAnchors(anchors)
  }

  // ---------------------------------------------------------------- 画质治理

  private trackQuality(delta: number) {
    if (this.phase !== 'live') return

    this.frameAccum += delta
    this.frameCount += 1
    if (this.frameCount < 120) return

    const avgMs = (this.frameAccum / this.frameCount) * 1000
    this.frameAccum = 0
    this.frameCount = 0

    if (avgMs > 19 && this.qualityTier < 2) {
      this.qualityTier += 1
      this.recoverStreak = 0
      this.applyQualityTier()
    } else if (avgMs < 13 && this.qualityTier > 0) {
      this.recoverStreak += 1
      if (this.recoverStreak >= 3) {
        this.qualityTier -= 1
        this.recoverStreak = 0
        this.applyQualityTier()
      }
    } else {
      this.recoverStreak = 0
    }
  }

  private applyQualityTier() {
    if (!this.renderer) return

    const scale = this.qualityTier === 0 ? 1 : this.qualityTier === 1 ? 0.82 : 0.66
    this.renderer.setPixelRatio(this.profile.basePixelRatio * scale)
    if (this.starMaterial) this.starMaterial.uniforms.uPixelRatio!.value = this.profile.basePixelRatio * scale
    this.syncStarViewport()
    this.post?.setCinematicExtras(this.qualityTier < 2)
    if (this.keyLight && this.profile.shadows) {
      this.keyLight.castShadow = this.qualityTier < 2
    }
    this.resize()
  }

  // ---------------------------------------------------------------- 基础设施

  private resize() {
    if (!this.host || !this.renderer || !this.camera) return

    const width = Math.max(this.host.clientWidth, 1)
    const height = Math.max(this.host.clientHeight, 1)
    this.renderer.setSize(width, height, false)
    this.post?.setSize(width, height)
    this.syncStarViewport()
    this.camera.aspect = width / height
    this.camera.updateProjectionMatrix()
  }

  destroy() {
    this.destroyed = true
    this.stopAnimation()
    this.resizeObserver?.disconnect()
    document.removeEventListener('visibilitychange', this.handleVisibilityChange)

    const disposeObject = (object: THREE.Object3D | null) => {
      if (!object) return
      object.traverse((item) => {
        const mesh = item as THREE.Mesh
        if (!mesh.isMesh && !(item as THREE.Points).isPoints) return
        mesh.geometry?.dispose()
        const materials = Array.isArray(mesh.material) ? mesh.material : [mesh.material]
        materials.filter(Boolean).forEach((material) => {
          const map = (material as THREE.MeshBasicMaterial).map
          map?.dispose()
          material.dispose()
        })
      })
    }

    if (this.scene) {
      disposeObject(this.scene)
    }
    this.groundMaterial?.dispose()
    this.pmremTexture?.dispose()
    this.post?.dispose()
    this.renderer?.dispose()
    this.renderer?.domElement.remove()

    this.renderer = null
    this.scene = null
    this.camera = null
    this.post = null
    this.model = null
    this.modelGroup = null
    this.holo = null
    this.starMaterial = null
    this.stripMaterial = null
    this.wallGrazingLight = null
    this.wallMaterial = null
    this.wallTextMaterial = null
    this.wallTextSheenMaterial = null
    this.floorBaseMaterial = null
    this.catcherMaterial = null
    this.contactMaterial = null
    this.orbitLightA = null
    this.orbitLightB = null
    this.eyeMaterials = []
    this.midStage = null
    this.topStage = null
    this.followers = []
    this.retractableGuides = []
    this.wheels = []
    this.anchorNodes.clear()
  }
}
