<script setup lang="ts">
import { gsap } from 'gsap'
import type { LinkState } from '@/composables/useRobot'
import { createOverviewRuntimeScope, useOverviewRuntime } from '@/composables/useOverviewRuntime'
import KeyboardControls from '@/components/console/KeyboardControls.vue'

type StageKey = 'video' | 'map' | 'cloud'
type DrawerMode = 'settings' | 'operators' | 'diagnostics'

interface VideoDiagnostics {
  state: LinkState
  source: string
  sourceMeta: string
  fps: number | null
  frameHeld: boolean
  error: string
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

const emit = defineEmits<{ (e: 'exit'): void }>()

const stages = [
  { key: 'video', label: '视频', icon: 'lucide:video', standby: 'VIDEO · STANDBY' },
  { key: 'map', label: '态势', icon: 'lucide:map', standby: 'SITUATION · STANDBY' },
  { key: 'cloud', label: '感知', icon: 'lucide:box', standby: 'SENSE · STANDBY' },
] satisfies Array<{ key: StageKey; label: string; icon: string; standby: string }>

const requestedStage = useState<StageKey>('console:requested-stage', () => 'video')
const activeStage = ref<StageKey>(requestedStage.value)
const pipStage = ref<StageKey>('map')
const current = computed(() => stages.find(s => s.key === activeStage.value) ?? stages[0]!)
const pipCurrent = computed(() => stages.find(s => s.key === pipStage.value) ?? stages[1]!)
const videoState = ref<LinkState>('off')
const pointCloudState = ref<LinkState>('off')
const drawerOpen = ref(false)
const drawerMode = useState<DrawerMode>('console:drawer-mode', () => 'settings')
if (!['settings', 'operators', 'diagnostics'].includes(drawerMode.value)) drawerMode.value = 'settings'
const drawerHover = ref(false)
const drawerFocus = ref(false)
const videoDiagnostics = ref<VideoDiagnostics>({
  state: 'off',
  source: '',
  sourceMeta: '',
  fps: null,
  frameHeld: false,
  error: '',
})
const pointCloudDiagnostics = ref<PointCloudDiagnostics>({
  state: 'off',
  streamName: '',
  topic: '',
  displayedPointCount: 0,
  sourcePointCount: 0,
  validPointCount: 0,
  filteredPointCount: 0,
  dataFps: 0,
  renderFps: 0,
  renderMode: 'idle',
  lastFrameAt: 0,
  paused: false,
  frameHeld: false,
  cachedSnapshot: false,
  awaitingFirstFrame: false,
  firstFrameTimedOut: false,
  error: '',
  maxVisiblePoints: 0,
  colorMode: 'solid',
  viewPreset: 'scan-top',
})
const { session } = useAuth()
const {
  update: updateOverviewRuntime,
  flush: flushOverviewRuntime,
  clear: clearOverviewRuntime,
} = useOverviewRuntime()
const runtimeConfig = useRuntimeConfig()
const overviewRuntimeScope = computed(() => createOverviewRuntimeScope(session.value, runtimeConfig.public.apiBase))
const hasLiveMedia = computed(() => (
  (activeStage.value === 'video' && (videoState.value === 'ok' || videoDiagnostics.value.frameHeld))
  || (activeStage.value === 'cloud' && (
    pointCloudState.value === 'ok'
    || pointCloudDiagnostics.value.frameHeld
    || pointCloudDiagnostics.value.cachedSnapshot
  ))
))
const {
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
} = useTelemetry()
const telemetryDiagnosticError = computed(() => mapFrameIssue.value || telemetryError.value)
const activeMediaState = computed<LinkState>(() => {
  if (activeStage.value === 'video') return videoState.value
  if (activeStage.value === 'cloud') return pointCloudState.value
  return mapState.value
})

/* 切到 PiP 里的视图时改为互换，保证主舞台与小窗内容不重复 */
function selectStage(key: StageKey) {
  if (key === pipStage.value) pipStage.value = activeStage.value
  activeStage.value = key
  requestedStage.value = key
}

function swapPip() {
  const prev = activeStage.value
  activeStage.value = pipStage.value
  pipStage.value = prev
}

/* P3a：CommandHub 移动接入（桌面键盘 + 移动端摇杆/急停），离线时控件锁定 */
const estopRef = ref<{ flash: () => void } | null>(null)
const {
  apiState,
  cmdState,
  operatorState,
  isSelf,
  canControl,
  username,
  commanded,
  controlError,
  speedLimit,
  visual,
  keyboard,
  estopSignal,
  setJoystick,
  releaseJoystick,
  estop,
  refreshOperatorState,
  listOperatorUsers,
  assignOperator,
  reclaimOperator,
} = useRobot({
  onEstop: () => estopRef.value?.flash(),
})
const canNavigate = computed(() => (
  isSelf.value && (session.value?.permissions.includes('robot.control') ?? false)
))
const isSystemAdmin = computed(() => session.value?.permissions.includes('system.admin') ?? false)

const links = computed(() => [
  { key: 'API', state: apiState.value, title: `C.WebApi 登录态：${apiState.value}` },
  {
    key: 'ROS',
    state: cmdState.value,
    title: controlError.value || `控制通道 CommandHub（→ rosbridge）：${cmdState.value}`,
  },
  { key: 'VID', state: videoState.value, title: `视频通道（SRS WebRTC / Rosbridge / 局域网）：${videoState.value}` },
  { key: 'PCD', state: pointCloudState.value, title: `PointCloudHub：${pointCloudState.value}` },
])

/* 概览只读取这份最近会话快照，不在首页重复建立实时连接。 */
watch(
  [overviewRuntimeScope, apiState, cmdState, mapState, videoState, pointCloudState, batteryPercent, batteryVoltage],
  () => {
    const scope = overviewRuntimeScope.value
    if (!scope) {
      clearOverviewRuntime()
      return
    }
    updateOverviewRuntime(scope, {
      batteryPercent: batteryPercent.value,
      batteryVoltage: batteryVoltage.value,
      links: {
        API: apiState.value,
        CMD: cmdState.value,
        MAP: mapState.value,
        VID: videoState.value,
        PCD: pointCloudState.value,
      },
    })
  },
  { immediate: true },
)

function onJoyMove(v: { x: number; y: number; force: number }) {
  setJoystick(v.x, v.y, v.force)
  poke()
}

function onJoyRelease() {
  releaseJoystick()
}

/* 闲时沉浸引擎：抽屉关闭且 3s 无输入时完全隐藏 HUD，任何输入唤回。 */
const isIdle = ref(false)
const hudReady = ref(false)
let idleTimer: ReturnType<typeof setTimeout> | undefined

function poke() {
  isIdle.value = false
  clearTimeout(idleTimer)
  if (hudReady.value && !drawerOpen.value && !drawerHover.value && !drawerFocus.value) {
    idleTimer = setTimeout(() => (isIdle.value = true), 3000)
  }
}

function toggleDrawer() {
  drawerOpen.value = !drawerOpen.value
  poke()
}

function selectDrawerMode(mode: DrawerMode) {
  drawerMode.value = mode
  drawerOpen.value = true
  poke()
}

function onVideoDiagnostics(value: VideoDiagnostics) {
  videoDiagnostics.value = value
}

function onPointCloudDiagnostics(value: PointCloudDiagnostics) {
  pointCloudDiagnostics.value = value
}

function onDrawerPointerEnter() {
  drawerHover.value = true
  poke()
}

function onDrawerPointerLeave() {
  drawerHover.value = false
  poke()
}

function onDrawerFocusIn() {
  drawerFocus.value = true
  poke()
}

function onDrawerFocusOut(event: FocusEvent) {
  const next = event.relatedTarget
  const root = event.currentTarget as HTMLElement
  if (next instanceof Node && root.contains(next)) return
  drawerFocus.value = false
  poke()
}

/* 层运动 = 鼠标视差（HUD ±3 / 世界 ∓6）+ 方向倾斜（世界反向平移 6px、侧倾 0.8°，HUD 顺向 2px） */
const consoleEl = ref<HTMLElement | null>(null)
const pointerN = { x: 0, y: 0 }
const driveN = { x: 0, y: 0 }
let applyLayers: (() => void) | null = null

function onPointerMove(e: PointerEvent) {
  poke()
  if (!applyLayers || !consoleEl.value) return
  const r = consoleEl.value.getBoundingClientRect()
  pointerN.x = Math.max(-1, Math.min(1, (e.clientX - r.left - r.width / 2) / (r.width / 2)))
  pointerN.y = Math.max(-1, Math.min(1, (e.clientY - r.top - r.height / 2) / (r.height / 2)))
  applyLayers()
}

watch(visual, (v) => {
  driveN.x = v.x
  driveN.y = v.y
  applyLayers?.()
})

/* 与 app.vue 的 chrome 收回时长（0.32s）对齐 */
const CHROME_DELAY = 0.34

let mm: gsap.MatchMedia | undefined
let entranceTl: gsap.core.Timeline | null = null
let consoleKeyListenerActive = false

function activateConsoleKeyListener() {
  if (consoleKeyListenerActive) return
  consoleKeyListenerActive = true
  window.addEventListener('keydown', poke)
}

function deactivateConsoleKeyListener() {
  if (!consoleKeyListenerActive) return
  consoleKeyListenerActive = false
  window.removeEventListener('keydown', poke)
  clearTimeout(idleTimer)
  isIdle.value = false
}

/* 慢机型保险：进场未完成时用户已开始操作，直接快进到完成态 */
function onPress() {
  poke()
  if (!hudReady.value && entranceTl) entranceTl.progress(1)
}

onMounted(() => {
  activateConsoleKeyListener()

  mm = gsap.matchMedia()

  mm.add('(prefers-reduced-motion: no-preference)', () => {
    gsap.set('.hud-el', {
      autoAlpha: 0,
      x: (_i: number, el: Element) => Number((el as HTMLElement).dataset.fx ?? 0),
      y: (_i: number, el: Element) => Number((el as HTMLElement).dataset.fy ?? 0),
    })

    const tl = gsap.timeline({
      delay: CHROME_DELAY,
      defaults: { ease: 'power3.out' },
      onComplete: () => {
        hudReady.value = true
        poke()
      },
    })
    entranceTl = tl
    tl.fromTo(
      '.stage-bracket path',
      { strokeDashoffset: 16 },
      { strokeDashoffset: 0, duration: 0.25, stagger: 0.04 },
    )
      .to(
        '.hud-el',
        { autoAlpha: 1, x: 0, y: 0, duration: 0.45, stagger: 0.06, clearProps: 'all' },
        '-=0.05',
      )
      .from('.hud-microlabel', { autoAlpha: 0, duration: 0.2, stagger: 0.04, clearProps: 'all' }, '-=0.3')
      .from(
        '.conn-dot',
        { scale: 0.4, autoAlpha: 0, duration: 0.3, ease: 'back.out(2)', stagger: 0.08, clearProps: 'all' },
        '-=0.15',
      )
      .from('.vitals-fill', { scaleX: 0, duration: 0.4, ease: 'power2.out' }, '-=0.2')

    const px = gsap.quickTo('.hud-parallax', 'x', { duration: 0.5, ease: 'power3.out' })
    const py = gsap.quickTo('.hud-parallax', 'y', { duration: 0.5, ease: 'power3.out' })
    const wx = gsap.quickTo('.stage-world', 'x', { duration: 0.5, ease: 'power3.out' })
    const wy = gsap.quickTo('.stage-world', 'y', { duration: 0.5, ease: 'power3.out' })
    const wr = gsap.quickTo('.stage-world', 'rotation', { duration: 0.6, ease: 'power3.out' })
    applyLayers = () => {
      px(pointerN.x * 3 + driveN.x * 2)
      py(pointerN.y * 3 + driveN.y * 2)
      wx(-pointerN.x * 6 - driveN.x * 6)
      wy(-pointerN.y * 6 - driveN.y * 6)
      wr(-driveN.x * 0.8)
    }

    return () => {
      applyLayers = null
    }
  })

  mm.add('(prefers-reduced-motion: reduce)', () => {
    hudReady.value = true
    poke()
  })
})

onActivated(() => {
  activateConsoleKeyListener()
  if (hudReady.value) poke()
})

onDeactivated(() => {
  flushOverviewRuntime()
  deactivateConsoleKeyListener()
})

onUnmounted(() => {
  flushOverviewRuntime()
  deactivateConsoleKeyListener()
  mm?.revert()
})
</script>

<template>
  <div
    ref="consoleEl"
    class="console flex-1"
    :class="{ 'is-idle': isIdle, 'hud-ready': hudReady, 'has-live-media': hasLiveMedia }"
    @pointermove="onPointerMove"
    @pointerdown.capture="onPress"
  >
    <!-- 主舞台：brackets 为固定取景框，world 层随鼠标视差 -->
    <div class="stage">
      <div class="stage-world">
        <Transition name="stage" mode="out-in">
          <ConsoleVideoStage
            v-if="current.key === 'video'"
            key="video"
            :camera-image="cameraImage"
            :camera-state="mapState"
            @state="videoState = $event"
            @diagnostics="onVideoDiagnostics"
          />
          <ConsoleMapStage
            v-else-if="current.key === 'map'"
            key="map"
            :state="mapState"
            :map="mapFrame"
            :map-source="mapFrameSource"
            :map-cached-at="mapFrameCachedAt"
            :robot-pose="robotPose"
            :particles="particles"
            :path="globalPath"
            :local-plan="localPlan"
            :scan="scan"
            :global-costmap="globalCostmap"
            :local-costmap="localCostmap"
            :map-patch="mapPatch"
            :telemetry-metrics="telemetryMetrics"
            :error="telemetryError"
            :map-issue="mapFrameIssue"
            :can-navigate="canNavigate"
            :set-goal="setGoal"
            :set-initial-pose="setInitialPose"
          />
          <ConsolePointCloudStage
            v-else-if="current.key === 'cloud'"
            key="cloud"
            @state="pointCloudState = $event"
            @diagnostics="onPointCloudDiagnostics"
          />
        </Transition>
      </div>

      <div class="stage-scrim" aria-hidden="true"></div>

      <svg
        v-for="c in ['tl', 'tr', 'br', 'bl']"
        :key="c"
        class="stage-bracket"
        :class="`stage-bracket-${c}`"
        viewBox="0 0 10 10"
        aria-hidden="true"
      >
        <path d="M 1 9 V 1 H 9" fill="none" stroke="currentColor" stroke-width="1.5" stroke-dasharray="16" />
      </svg>
    </div>

    <div class="hud-parallax">
      <!-- ⑨ 开放式退出控件：Esc 同效，不占用玻璃卡片预算 -->
      <button
        class="hud-el hud-exit"
        data-fx="-12"
        aria-label="退出控制台（Esc）"
        title="退出控制台（Esc）"
        @click="emit('exit')"
      >
        <Icon class="hud-exit-mark" name="lucide:arrow-left" size="16" aria-hidden="true" />
      </button>

      <!-- ① 遥测数据带 -->
      <ConsoleTelemetryStrip
        :odometry="odometry"
        :safe-twist="safeTwist"
        :state="mapState"
        :error="telemetryError"
        class="hud-el hud-tele"
        data-fy="-12"
      />

      <!-- ③ 视图切换（清单式） -->
      <ConsoleViewSwitcher
        :model-value="activeStage"
        :stages="stages"
        class="hud-el hud-switcher"
        data-fx="-12"
        @update:model-value="selectStage($event as StageKey)"
      />

      <!-- ④ PiP 小窗（点击与主舞台互换） -->
      <ConsolePipWindow :stage="pipCurrent" class="hud-el hud-pip" data-fx="12" @swap="swapPip" />

      <!-- ② 生命体征簇（连接状态 / 操作者 / 速度均为真值） -->
      <ConsoleVitalsCluster
        :battery-percent="batteryPercent"
        :battery-state="mapState"
        :battery-voltage="batteryVoltage"
        :battery-error="telemetryError"
        :speed-current="commanded"
        :speed-limit="speedLimit"
        :username="username || 'OFFLINE'"
        :links="links"
        class="hud-el hud-vitals"
        data-fy="12"
      />

      <!-- ⑤⑥ 控制舱：桌面显示键盘映射，移动端显示摇杆 + 急停 -->
      <div class="hud-el hud-pod" data-fy="12">
        <div class="pod-desktop-controls">
          <KeyboardControls
            :state="keyboard"
            :disabled="!canControl"
            :estop-signal="estopSignal"
          />
        </div>
        <div class="pod-mobile-controls">
          <span class="hud-microlabel" aria-hidden="true">CTRL</span>
          <ConsoleJoystickPad :driven="visual" :disabled="!canControl" @move="onJoyMove" @release="onJoyRelease" />
          <span class="pod-link" aria-hidden="true"></span>
          <ConsoleEStopButton ref="estopRef" :disabled="!canControl" @stop="estop(false)" />
        </div>
      </div>

      <!-- ⑦ 控制台抽屉：舞台设置通过 Teleport 注入，诊断状态在本地汇总。 -->
      <ConsoleHudPanel
        class="hud-el hud-drawer"
        :class="{ 'is-open': drawerOpen }"
        data-fx="12"
        chamfer="tl"
        @pointerenter="onDrawerPointerEnter"
        @pointerleave="onDrawerPointerLeave"
        @focusin="onDrawerFocusIn"
        @focusout="onDrawerFocusOut"
      >
        <div class="drawer-shell">
          <button
            class="drawer-btn"
            :aria-expanded="drawerOpen"
            aria-controls="console-drawer-content"
            :aria-label="drawerOpen ? '收起控制台抽屉' : '展开控制台抽屉'"
            :title="drawerOpen ? '收起控制台抽屉' : '展开控制台抽屉'"
            @click="toggleDrawer"
          >
            <span class="media-status-dot" :class="`is-${activeMediaState}`" aria-hidden="true"></span>
            <Icon
              class="drawer-chevron"
              :name="drawerOpen ? 'lucide:chevrons-right' : 'lucide:chevrons-left'"
              size="14"
            />
          </button>
          <div class="drawer-content" :aria-hidden="!drawerOpen">
            <div id="console-drawer-content" class="drawer-content-inner">
              <div class="drawer-tabs" role="tablist" aria-label="控制台抽屉视图">
                <button
                  type="button"
                  role="tab"
                  :class="{ active: drawerMode === 'settings' }"
                  :aria-selected="drawerMode === 'settings'"
                  aria-controls="drawer-settings-panel"
                  @click="selectDrawerMode('settings')"
                >
                  <Icon name="lucide:sliders-horizontal" size="13" />
                  <span>设置</span>
                </button>
                <button
                  type="button"
                  role="tab"
                  :class="{ active: drawerMode === 'operators' }"
                  :aria-selected="drawerMode === 'operators'"
                  aria-controls="drawer-operators-panel"
                  @click="selectDrawerMode('operators')"
                >
                  <Icon name="lucide:user-round-cog" size="13" />
                  <span>操作者</span>
                </button>
                <button
                  type="button"
                  role="tab"
                  :class="{ active: drawerMode === 'diagnostics' }"
                  :aria-selected="drawerMode === 'diagnostics'"
                  aria-controls="drawer-diagnostics-panel"
                  @click="selectDrawerMode('diagnostics')"
                >
                  <Icon name="lucide:activity" size="13" />
                  <span>诊断</span>
                </button>
              </div>

              <div
                v-show="drawerMode === 'settings'"
                id="drawer-settings-panel"
                role="tabpanel"
                :aria-hidden="drawerMode !== 'settings'"
              >
                <div id="media-drawer-content" class="drawer-media-slot"></div>
              </div>

              <ConsoleOperatorPanel
                v-show="drawerMode === 'operators'"
                id="drawer-operators-panel"
                role="tabpanel"
                :aria-hidden="drawerMode !== 'operators'"
                :api-state="apiState"
                :operator="operatorState"
                :is-admin="isSystemAdmin"
                :refresh="refreshOperatorState"
                :list-users="listOperatorUsers"
                :assign="assignOperator"
                :reclaim="reclaimOperator"
              />

              <ConsoleDiagnosticsPanel
                v-show="drawerMode === 'diagnostics'"
                id="drawer-diagnostics-panel"
                role="tabpanel"
                :aria-hidden="drawerMode !== 'diagnostics'"
                :api-state="apiState"
                :cmd-state="cmdState"
                :map-state="mapState"
                :video-state="videoState"
                :point-cloud-state="pointCloudState"
                :map-frame-source="mapFrameSource"
                :map-cached-at="mapFrameCachedAt"
                :telemetry-metrics="telemetryMetrics"
                :video="videoDiagnostics"
                :point-cloud="pointCloudDiagnostics"
                :telemetry-error="telemetryDiagnosticError"
                :control-error="controlError"
              />
            </div>
          </div>
        </div>
      </ConsoleHudPanel>
    </div>
  </div>
</template>

<style>
.console {
  --drawer-surface: rgba(255, 255, 255, 0.985);
  --drawer-tab-surface: rgba(255, 255, 255, 0.72);
  --drawer-edge: rgba(23, 24, 28, 0.12);
  --drawer-shadow: rgba(17, 24, 39, 0.12);
  position: relative;
  overflow: hidden;
  background: var(--secondary);
}

.console.has-live-media {
  --foreground: #f8fafc;
  --muted-foreground: #d0d5dd;
  --border: rgba(255, 255, 255, 0.26);
  --ornament: rgba(255, 255, 255, 0.58);
  --ornament-faint: rgba(255, 255, 255, 0.16);
  --glass-bg: rgba(13, 16, 22, 0.42);
  --glass-bg-strong: rgba(13, 16, 22, 0.56);
  --glass-border: rgba(255, 255, 255, 0.18);
  --drawer-surface: rgba(13, 16, 22, 0.96);
  --drawer-tab-surface: rgba(13, 16, 22, 0.68);
  --drawer-edge: rgba(255, 255, 255, 0.22);
  --drawer-shadow: rgba(0, 0, 0, 0.34);
}

.stage {
  position: absolute;
  inset: 0;
}

.stage-world {
  position: absolute;
  inset: -16px;
  display: flex;
  align-items: center;
  justify-content: center;
  background-image: radial-gradient(var(--ornament-faint) 1px, transparent 1px);
  background-size: 24px 24px;
}

.stage-scrim {
  position: absolute;
  inset: 0;
  z-index: 1;
  pointer-events: none;
  opacity: 0;
  background:
    linear-gradient(90deg, rgba(8, 10, 14, 0.5), transparent 24%, transparent 76%, rgba(8, 10, 14, 0.5)),
    linear-gradient(180deg, rgba(8, 10, 14, 0.42), transparent 27%, transparent 58%, rgba(8, 10, 14, 0.68));
  transition: opacity 0.24s ease;
}

.console.has-live-media .stage-scrim {
  opacity: 1;
}

.stage-bracket {
  position: absolute;
  z-index: 2;
  width: 10px;
  height: 10px;
  color: var(--ornament);
  transition: opacity 0.25s ease;
}

.stage-bracket-tl { top: 14px; left: 14px; }
.stage-bracket-tr { top: 14px; right: 14px; transform: rotate(90deg); }
.stage-bracket-br { bottom: 14px; right: 14px; transform: rotate(180deg); }
.stage-bracket-bl { bottom: 14px; left: 14px; transform: rotate(270deg); }

.stage-standby {
  display: flex;
  flex-direction: column;
  align-items: center;
  gap: 10px;
  color: var(--ornament);
  font-size: 11px;
  letter-spacing: 0.3em;
}

.stage-enter-active {
  transition:
    opacity 0.18s ease,
    transform 0.18s ease;
}

.stage-leave-active {
  transition: opacity 0.12s ease;
}

.stage-enter-from {
  opacity: 0;
  transform: scale(0.98);
}

.stage-leave-to {
  opacity: 0;
}

/* ---------- HUD 层（视差容器） ---------- */

.hud-parallax {
  position: absolute;
  inset: 0;
  z-index: 10;
  pointer-events: none;
}

.hud-parallax .hud-el {
  pointer-events: auto;
}

.hud-el {
  position: absolute;
}

.hud-exit {
  top: var(--hud-gap);
  left: var(--hud-gap);
  display: grid;
  place-items: center;
  width: 40px;
  height: 40px;
  padding: 0;
  border: 0;
  background: transparent;
  color: var(--muted-foreground);
  cursor: pointer;
  transition: color 0.15s ease;
}

.hud-exit::before,
.hud-exit::after {
  position: absolute;
  width: 10px;
  height: 10px;
  border: 0 solid var(--ornament);
  content: '';
  opacity: 0.72;
  transition:
    width 0.18s ease,
    height 0.18s ease,
    border-color 0.18s ease,
    opacity 0.18s ease;
}

.hud-exit::before {
  top: 2px;
  left: 2px;
  border-top-width: 1px;
  border-left-width: 1px;
}

.hud-exit::after {
  right: 2px;
  bottom: 2px;
  border-right-width: 1px;
  border-bottom-width: 1px;
}

.hud-exit-mark {
  width: 16px;
  height: 16px;
  flex: none;
  transition: transform 0.18s ease;
}

.hud-exit:hover,
.hud-exit:focus-visible {
  color: var(--foreground);
}

.hud-exit:hover::before,
.hud-exit:hover::after,
.hud-exit:focus-visible::before,
.hud-exit:focus-visible::after {
  width: 15px;
  height: 15px;
  border-color: var(--primary);
  opacity: 1;
}

.hud-exit:hover .hud-exit-mark,
.hud-exit:focus-visible .hud-exit-mark {
  transform: translateX(-3px);
}

.hud-exit:active .hud-exit-mark {
  transform: translateX(-4px) scale(0.9);
}

.hud-tele {
  top: var(--hud-gap);
  left: 0;
  right: 0;
  margin-inline: auto;
  width: fit-content;
}

.hud-switcher {
  top: calc(var(--hud-gap) + 48px);
  left: var(--hud-gap);
}

.hud-pip {
  top: var(--hud-gap);
  right: var(--hud-gap);
}

.hud-vitals {
  bottom: var(--hud-gap);
  left: var(--hud-gap);
}

/* ⑤⑥ 控制舱 */
.hud-pod {
  bottom: var(--hud-gap);
  right: var(--hud-gap);
  display: flex;
  flex-direction: column;
  align-items: center;
}

.pod-mobile-controls {
  display: none;
  flex-direction: column;
  align-items: center;
}

.pod-mobile-controls .hud-microlabel {
  align-self: flex-start;
  margin-bottom: 8px;
}

.pod-link {
  width: 1px;
  height: 12px;
  margin: 2px 0;
  background: var(--border);
}

.hud-drawer {
  --drawer-width: min(320px, calc(100vw - 44px));
  --drawer-handle-width: 38px;
  --chamfer: 12px;
  top: 50%;
  right: 0;
  transform: translateY(-50%);
  transform-origin: right center;
}

.hud-drawer.is-open {
  transform: translate(-1px, -50%);
}

.hud-drawer:hover,
.hud-drawer:focus-within {
  transform: translate(-2px, -50%);
}

.hud-drawer .hud-panel-bd {
  border-radius: 0;
  background: transparent;
  filter: drop-shadow(0 0 0 transparent);
  transition:
    background-color 0.2s ease,
    filter 0.3s ease;
}

.hud-drawer .hud-panel-face {
  overflow: hidden;
  border-radius: 0;
  padding: 0;
  background: transparent;
  backdrop-filter: none;
  transition:
    background-color 0.22s ease,
    backdrop-filter 0.22s ease;
}

.hud-drawer:not(.is-open):hover .hud-panel-bd,
.hud-drawer:not(.is-open):focus-within .hud-panel-bd,
.hud-drawer.is-open .hud-panel-bd {
  background: var(--drawer-edge);
}

.hud-drawer:not(.is-open):hover .hud-panel-face,
.hud-drawer:not(.is-open):focus-within .hud-panel-face {
  background: var(--drawer-tab-surface);
  backdrop-filter: blur(14px) saturate(145%);
}

.hud-drawer.is-open .hud-panel-bd {
  filter: drop-shadow(-14px 10px 24px var(--drawer-shadow));
}

.hud-drawer.is-open .hud-panel-face,
.hud-drawer.is-open:hover .hud-panel-face,
.hud-drawer.is-open:focus-within .hud-panel-face {
  background: var(--drawer-surface);
  backdrop-filter: blur(22px) saturate(165%);
}

.hud-drawer > .hud-brackets {
  inset: -3px auto -3px -3px;
  width: 14px;
  transition: opacity 0.2s ease;
}

.hud-drawer > .hud-brackets i {
  width: 12px;
  height: 12px;
  border-color: var(--primary);
}

.hud-drawer > .hud-brackets i:nth-child(2),
.hud-drawer > .hud-brackets i:nth-child(4) {
  display: none;
}

.hud-drawer:hover > .hud-brackets,
.hud-drawer:focus-within > .hud-brackets {
  opacity: 0.68;
}

.hud-drawer.is-open > .hud-brackets {
  opacity: 0.9;
}

.drawer-shell {
  position: relative;
  display: flex;
  align-items: stretch;
}

.drawer-btn {
  position: relative;
  display: flex;
  align-items: center;
  justify-content: center;
  flex-direction: column;
  gap: 14px;
  width: var(--drawer-handle-width);
  min-height: 116px;
  flex: none;
  border: 0;
  padding: 0;
  background: transparent;
  color: var(--muted-foreground);
  cursor: pointer;
  transition: color 0.18s ease;
}

.drawer-btn::before,
.drawer-btn::after {
  position: absolute;
  left: 0;
  content: '';
  pointer-events: none;
}

.drawer-btn::before {
  top: 18px;
  bottom: 18px;
  width: 1px;
  background: linear-gradient(transparent, var(--ornament) 24%, var(--ornament) 76%, transparent);
  opacity: 0.82;
  transform: scaleY(0.58);
  transition:
    background-color 0.2s ease,
    opacity 0.2s ease,
    transform 0.24s cubic-bezier(0.22, 1, 0.36, 1);
}

.drawer-btn::after {
  top: 50%;
  width: 7px;
  height: 1px;
  background: var(--ornament);
  transform: translateY(-0.5px);
  transition:
    width 0.22s cubic-bezier(0.22, 1, 0.36, 1),
    background-color 0.18s ease,
    box-shadow 0.18s ease;
}

.drawer-btn:hover,
.drawer-btn:focus-visible {
  outline: none;
  color: var(--foreground);
}

.hud-drawer:hover .drawer-btn::before,
.hud-drawer:focus-within .drawer-btn::before,
.hud-drawer.is-open .drawer-btn::before {
  background: var(--primary);
  opacity: 0.9;
  transform: scaleY(1);
}

.hud-drawer:hover .drawer-btn::after,
.hud-drawer:focus-within .drawer-btn::after,
.hud-drawer.is-open .drawer-btn::after {
  width: 13px;
  background: var(--primary);
  box-shadow: 5px 0 14px color-mix(in srgb, var(--primary) 28%, transparent);
}

.drawer-btn .media-status-dot {
  transition:
    transform 0.2s cubic-bezier(0.22, 1, 0.36, 1),
    box-shadow 0.2s ease;
}

.hud-drawer:hover .drawer-btn .media-status-dot,
.hud-drawer:focus-within .drawer-btn .media-status-dot {
  box-shadow: 0 0 0 5px color-mix(in srgb, var(--foreground) 8%, transparent);
  transform: scale(1.28);
}

.drawer-chevron {
  opacity: 0.72;
  transition:
    opacity 0.18s ease,
    transform 0.24s cubic-bezier(0.22, 1, 0.36, 1);
}

.hud-drawer:hover .drawer-chevron,
.hud-drawer:focus-within .drawer-chevron {
  opacity: 1;
}

.hud-drawer:not(.is-open):hover .drawer-chevron,
.hud-drawer:not(.is-open):focus-within .drawer-chevron {
  transform: translateX(-3px);
}

.hud-drawer.is-open:hover .drawer-chevron,
.hud-drawer.is-open:focus-within .drawer-chevron {
  transform: translateX(3px);
}

.hud-drawer:not(.is-open) .drawer-btn:active .drawer-chevron {
  transform: translateX(-4px) scale(0.84);
}

.hud-drawer.is-open .drawer-btn:active .drawer-chevron {
  transform: translateX(4px) scale(0.84);
}

.drawer-content {
  width: 0;
  max-height: 0;
  overflow: hidden;
  opacity: 0;
  visibility: hidden;
  transition:
    width 0.34s cubic-bezier(0.16, 1, 0.3, 1),
    max-height 0.34s cubic-bezier(0.16, 1, 0.3, 1),
    opacity 0.16s ease,
    visibility 0s linear 0.34s;
}

.hud-drawer.is-open .drawer-content {
  width: var(--drawer-width);
  max-height: min(78dvh, 640px);
  opacity: 1;
  visibility: visible;
  transition:
    width 0.34s cubic-bezier(0.16, 1, 0.3, 1),
    max-height 0.34s cubic-bezier(0.16, 1, 0.3, 1),
    opacity 0.18s ease 0.07s,
    visibility 0s;
}

.drawer-content-inner {
  position: relative;
  box-sizing: border-box;
  width: var(--drawer-width);
  max-height: min(78dvh, 640px);
  overflow-y: auto;
  padding: 14px;
  opacity: 0;
  transform: translateX(14px) scale(0.985);
  transform-origin: right center;
  transition:
    opacity 0.14s ease,
    transform 0.2s ease;
}

.drawer-content-inner::before {
  position: absolute;
  top: 0;
  right: 14px;
  left: 14px;
  height: 1px;
  background: linear-gradient(90deg, transparent, var(--primary) 38%, transparent);
  content: '';
  opacity: 0;
  transform: scaleX(0.18);
  transform-origin: right center;
  transition:
    opacity 0.2s ease,
    transform 0.38s cubic-bezier(0.16, 1, 0.3, 1);
}

.hud-drawer.is-open .drawer-content-inner {
  opacity: 1;
  transform: translateX(0) scale(1);
  transition:
    opacity 0.2s ease 0.09s,
    transform 0.34s cubic-bezier(0.16, 1, 0.3, 1) 0.04s;
}

.hud-drawer.is-open .drawer-content-inner::before {
  opacity: 0.5;
  transform: scaleX(1);
  transition-delay: 0.12s;
}

.drawer-media-slot {
  min-height: 132px;
}

.drawer-tabs {
  display: grid;
  grid-template-columns: repeat(3, minmax(0, 1fr));
  gap: 3px;
  margin-bottom: 13px;
  padding-bottom: 10px;
  border-bottom: 1px solid color-mix(in srgb, var(--border) 72%, transparent);
}

.drawer-tabs button {
  position: relative;
  display: inline-flex;
  align-items: center;
  justify-content: center;
  gap: 6px;
  min-width: 0;
  height: 30px;
  border: 0;
  background: transparent;
  color: var(--muted-foreground);
  font-size: 9px;
  cursor: pointer;
  transition:
    background-color 0.16s ease,
    color 0.16s ease;
}

.drawer-tabs button::after {
  position: absolute;
  right: 20%;
  bottom: -11px;
  left: 20%;
  height: 1px;
  background: var(--primary);
  content: '';
  opacity: 0;
  transform: scaleX(0.35);
  transition:
    opacity 0.16s ease,
    transform 0.2s cubic-bezier(0.22, 1, 0.36, 1);
}

.drawer-tabs button:hover,
.drawer-tabs button:focus-visible {
  background: color-mix(in srgb, var(--foreground) 7%, transparent);
  color: var(--foreground);
  outline: none;
}

.drawer-tabs button.active {
  color: var(--primary);
}

.drawer-tabs button.active::after {
  opacity: 0.9;
  transform: scaleX(1);
}

.drawer-empty-state {
  display: flex;
  align-items: center;
  justify-content: center;
  gap: 8px;
  min-height: 132px;
  color: var(--muted-foreground);
  font-size: 11px;
}

@media (max-width: 720px) {
  .pod-desktop-controls {
    display: none;
  }

  .pod-mobile-controls {
    display: flex;
  }
}

@media (max-width: 520px) {
  .hud-tele,
  .hud-pip {
    display: none;
  }

  .hud-vitals {
    --hud-bar-w: 150px;
  }

  .hud-vitals .v-identity {
    gap: 6px;
  }

  .hud-vitals .v-op {
    max-width: 64px;
  }

  .hud-vitals .v-link {
    font-size: 8.5px;
    letter-spacing: 0.04em;
  }
}

/* ---------- 闲时隐退（hud-ready 后才启用过渡，避免与进场 GSAP 抢 transform） ---------- */

.console.hud-ready .hud-el,
.console.hud-ready .hud-pod > * {
  transition:
    opacity 0.25s ease,
    transform 0.25s ease;
}

.console.is-idle .hud-el,
.console.is-idle .stage-bracket,
.console.is-idle .stage-scrim {
  opacity: 0;
  pointer-events: none;
}

.console.is-idle .hud-panel-face {
  backdrop-filter: none;
}

@media (prefers-reduced-motion: reduce) {
  .console .hud-el,
  .console .hud-pod > *,
  .hud-exit::before,
  .hud-exit::after,
  .hud-exit-mark,
  .hud-drawer .hud-panel-bd,
  .hud-drawer .hud-panel-face,
  .hud-drawer > .hud-brackets,
  .drawer-btn,
  .drawer-btn::before,
  .drawer-btn::after,
  .drawer-btn .media-status-dot,
  .drawer-chevron,
  .drawer-content,
  .drawer-content-inner,
  .drawer-content-inner::before,
  .drawer-tabs button,
  .drawer-tabs button::after {
    transition: none;
  }
}
</style>
