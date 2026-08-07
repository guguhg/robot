<script setup lang="ts">
import '@fontsource/chakra-petch/500.css'
import '@fontsource/chakra-petch/600.css'
import { computed, onBeforeUnmount, onMounted, ref } from 'vue'
import HengviewScrollRail from '~/components/hengview/HengviewScrollRail.vue'
import {
  HengviewScene,
  type HengviewAnchor,
  type HengviewPhase,
  type HengviewTelemetry,
} from '~/lib/hengview/hengview-scene'

useSeoMeta({
  title: '望衡 HENGVIEW｜桥桥友河智能装备',
  description: '望衡 HENGVIEW 监理巡检机器人的装配开机与工程机构展厅。',
  ogTitle: '望衡 HENGVIEW',
  ogDescription: '桥桥友河智能装备的望衡 HENGVIEW 机器人展厅。',
})

const trackEl = ref<HTMLElement | null>(null)
const hostEl = ref<HTMLElement | null>(null)

const loadingTarget = ref(0.02)
const loadingValue = ref(0)
const isLoading = ref(true)
const loaderCollapsing = ref(false)
const phase = ref<HengviewPhase>('loading')
const heroRevealed = ref(false)
const sceneError = ref('')
const scrollProgress = ref(0)
const reducedMotion = ref(false)

const telemetry = ref<HengviewTelemetry>({
  section: 0,
  scanPct: 0,
  liftMeters: 0,
  explodePct: 0,
  online: false,
})
const anchors = ref<HengviewAnchor[]>([])

const clamp01 = (value: number) => Math.min(1, Math.max(0, value))
const smoothstep = (value: number) => {
  const t = clamp01(value)
  return t * t * (3 - 2 * t)
}

const loadingPercent = computed(() =>
  Math.round(loadingValue.value * 100).toString().padStart(3, '0'),
)
const loaderLineStyle = computed(() => ({
  transform: `scaleX(${loadingValue.value})`,
}))

const isLive = computed(() => phase.value === 'live')
const isBooting = computed(() => phase.value === 'boot')
const railActive = computed(() => scrollProgress.value > 0.015)
const isLight = ref(false)

const toggleTheme = () => {
  isLight.value = !isLight.value
  scene?.setTheme(isLight.value ? 'light' : 'dark')
}

const statusText = computed(() => {
  if (sceneError.value) return 'FAULT'
  if (phase.value === 'loading') return 'PRELOAD'
  if (phase.value === 'boot') return `SCAN ${telemetry.value.scanPct.toString().padStart(3, '0')}%`
  return 'ONLINE'
})
const sectionText = computed(() => `SEC ${telemetry.value.section.toString().padStart(2, '0')} / 05`)
const liftText = computed(() => `LIFT +${telemetry.value.liftMeters.toFixed(2)}M`)

/** 段落文案窗口：进入-停驻-离场 */
const sectionStyle = (start: number, end: number, drift = 14) => {
  const p = clamp01((scrollProgress.value - start) / (end - start))
  const enter = smoothstep(p / 0.22)
  const leave = 1 - smoothstep((p - 0.78) / 0.22)
  const visibility = Math.min(enter, leave)
  const y = reducedMotion.value ? 0 : (1 - enter) * drift + (1 - leave) * -drift
  return {
    '--sec-opacity': visibility.toFixed(3),
    '--sec-y': `${y.toFixed(1)}px`,
    '--sec-blur': `${((1 - visibility) * 3).toFixed(2)}px`,
  }
}

const heroStyle = computed(() => {
  const departure = smoothstep((scrollProgress.value - 0.02) / 0.08)
  const entry = isLive.value ? 1 : 0
  return {
    '--sec-opacity': (entry * (1 - departure)).toFixed(3),
    '--sec-y': `${departure * -16}px`,
    '--sec-blur': '0px',
  }
})

const ctaStyle = computed(() => sectionStyle(0.94, 1.06))

const focusAnchors = computed(() => anchors.value.filter((a) => a.id && a.code.startsWith('A') && a.visible))
const structureAnchors = computed(() => anchors.value.filter((a) => a.code.startsWith('B') && a.visible))

const anchorStyle = (anchor: HengviewAnchor) => ({
  left: `${anchor.x}%`,
  top: `${anchor.y}%`,
  '--flip': anchor.x > 62 ? '-1' : '1',
})

/** 2D 动效层：测距刻度尺随滚动缓慢平移 */
const rulerStyle = computed(() => ({
  transform: `translateY(${(-6 - scrollProgress.value * 14).toFixed(2)}%)`,
}))

let scene: HengviewScene | null = null
let loadingFrame: number | null = null
let scrollFrame: number | null = null
let bootDelay: ReturnType<typeof setTimeout> | null = null
let previousWheelMultiplier = 1

const animateLoading = () => {
  loadingFrame = null
  const delta = loadingTarget.value - loadingValue.value
  loadingValue.value = Math.abs(delta) < 0.003 ? loadingTarget.value : loadingValue.value + delta * 0.12
  if (loadingValue.value < 0.999) loadingFrame = window.requestAnimationFrame(animateLoading)
}

const setLoadingTarget = (value: number) => {
  loadingTarget.value = clamp01(value)
  if (loadingFrame === null) loadingFrame = window.requestAnimationFrame(animateLoading)
}

const updateScroll = () => {
  scrollFrame = null
  const element = trackEl.value
  if (!element) return
  const available = Math.max(element.offsetHeight - window.innerHeight, 1)
  const progress = clamp01(-element.getBoundingClientRect().top / available)
  scrollProgress.value = progress
  scene?.setScrollProgress(progress)
}

const queueScroll = () => {
  if (scrollFrame !== null) return
  scrollFrame = window.requestAnimationFrame(updateScroll)
}

const backToTop = () => {
  const nuxt = useNuxtApp() as unknown as {
    $lenis?: { scrollTo: (target: number, options?: object) => void }
  }
  // 长时程 + 双端五次方缓动：回升读作一次沉稳的机位收回，而不是急坠
  const duration = 1.9 + scrollProgress.value * 1.7
  const easeInOutQuint = (t: number) => (t < 0.5 ? 16 * t ** 5 : 1 - (-2 * t + 2) ** 5 / 2)
  if (nuxt.$lenis && !reducedMotion.value) {
    nuxt.$lenis.scrollTo(0, { duration, easing: easeInOutQuint, lock: true })
  } else {
    window.scrollTo({ top: 0, behavior: reducedMotion.value ? 'auto' : 'smooth' })
  }
}

onMounted(() => {
  reducedMotion.value = window.matchMedia('(prefers-reduced-motion: reduce)').matches
  window.addEventListener('scroll', queueScroll, { passive: true })
  window.addEventListener('resize', queueScroll, { passive: true })
  queueScroll()

  // 滚动微限制：本页降低滚轮倍率，让分镜有"被握住"的重量感
  const lenis = (useNuxtApp() as unknown as { $lenis?: { options?: { wheelMultiplier?: number } } }).$lenis
  if (lenis?.options) {
    previousWheelMultiplier = lenis.options.wheelMultiplier ?? 1
    lenis.options.wheelMultiplier = 0.8
  }

  if (!hostEl.value) return

  scene = new HengviewScene(hostEl.value, {
    reducedMotion: reducedMotion.value,
    onProgress: setLoadingTarget,
    onPhase: (value) => {
      phase.value = value
      // 开机完成后错峰浮现英雄文案
      if (value === 'live' && !heroRevealed.value) {
        window.setTimeout(() => {
          heroRevealed.value = true
        }, reducedMotion.value ? 0 : 160)
      }
    },
    onTelemetry: (value) => {
      telemetry.value = value
    },
    onAnchors: (value) => {
      anchors.value = value
    },
    onReady: () => {
      setLoadingTarget(1)
      loaderCollapsing.value = true
      // 发丝线收拢后揭幕：加载与开机是同一束光
      bootDelay = setTimeout(() => {
        isLoading.value = false
        scene?.beginBoot()
      }, reducedMotion.value ? 80 : 620)
    },
    onError: (message) => {
      sceneError.value = message
      isLoading.value = false
    },
  })
  void scene.start()
  if (import.meta.dev) {
    ;(window as unknown as Record<string, unknown>).__hengviewScene = scene
  }
})

onBeforeUnmount(() => {
  if (loadingFrame !== null) window.cancelAnimationFrame(loadingFrame)
  if (scrollFrame !== null) window.cancelAnimationFrame(scrollFrame)
  if (bootDelay !== null) clearTimeout(bootDelay)
  window.removeEventListener('scroll', queueScroll)
  window.removeEventListener('resize', queueScroll)
  const lenis = (useNuxtApp() as unknown as { $lenis?: { options?: { wheelMultiplier?: number } } }).$lenis
  if (lenis?.options) lenis.options.wheelMultiplier = previousWheelMultiplier
  scene?.destroy()
  scene = null
})
</script>

<template>
  <main class="cl-page" :class="{ 'is-light': isLight }">
    <section ref="trackEl" class="cl-track" aria-label="望衡 HENGVIEW 装配开机序列">
      <div class="cl-stage">
        <div ref="hostEl" class="cl-stage__scene" aria-hidden="true" />

        <!-- 仪表边框 -->
        <div class="cl-frame" aria-hidden="true">
          <i class="is-tl" /><i class="is-tr" /><i class="is-bl" /><i class="is-br" />
        </div>

        <!-- 2D 动效层：测距刻度尺 + 周期扫描线（字体层之下，收敛） -->
        <div v-if="!sceneError" class="cl-fx" :class="{ 'is-live': isLive || isBooting }" aria-hidden="true">
          <div class="cl-fx__ruler is-left" :style="rulerStyle" />
          <div class="cl-fx__ruler is-right" :style="rulerStyle" />
          <div class="cl-fx__sweep" />
        </div>

        <!-- 顶部 HUD -->
        <header class="cl-hud" :class="{ 'is-online': isLive || isBooting }">
          <div class="cl-hud__brand">
            <span class="cl-hud__logo">HENGVIEW</span>
            <span class="cl-hud__unit">望衡 · HENGVIEW — 监理巡检机器人</span>
          </div>
          <div class="cl-hud__data" aria-live="off">
            <span>{{ statusText }}</span>
            <span>{{ sectionText }}</span>
            <span>{{ liftText }}</span>
          </div>
          <button
            v-if="isLive"
            type="button"
            class="cl-hud__theme"
            :aria-pressed="isLight"
            aria-label="切换展厅照明"
            @click="toggleTheme"
          >
            <span class="cl-hud__themeOpt" :class="{ 'is-on': !isLight }">DARK</span>
            <i class="cl-hud__themeSlash" aria-hidden="true">/</i>
            <span class="cl-hud__themeOpt" :class="{ 'is-on': isLight }">LIGHT</span>
          </button>
        </header>

        <!-- 加载态：中心发丝线 -->
        <Transition name="cl-loader">
          <div v-if="isLoading" class="cl-loader" :class="{ 'is-collapsing': loaderCollapsing }" aria-live="polite" aria-label="正在装载装配数据">
            <div class="cl-loader__line" :style="loaderLineStyle" />
            <p class="cl-loader__meta">
              <span>装配数据流 // GEOMETRY</span>
              <b>{{ loadingPercent }}</b>
            </p>
          </div>
        </Transition>

        <p v-if="sceneError" class="cl-error" role="status">{{ sceneError }} — 请刷新重试</p>

        <!-- 叙事覆盖层 -->
        <div v-if="!sceneError" class="cl-ui">
          <!-- 首屏 -->
          <section class="cl-sec cl-hero" :class="{ 'is-revealed': heroRevealed }" :style="heroStyle" aria-label="产品名称">
            <span class="cl-marker">HENGVIEW UNIT // 装配完成</span>
            <h1>望  衡</h1>
            <p class="cl-hero__sub">监理巡检机器人</p>
            <p class="cl-hero__spec">双目云台 / 三级顶升 2.0M / 全向舵轮</p>
          </section>

          <!-- 01 云台 -->
          <section class="cl-sec cl-card is-left" :style="sectionStyle(0.155, 0.325)">
            <span class="cl-marker">01 / OPTICS</span>
            <h2>双目云台</h2>
            <p>两组前向视觉，同一条视线。<br>云台在机体起伏中保持水平取景。</p>
          </section>

          <!-- 02 顶升（实时工作高度读数） -->
          <section class="cl-sec cl-card is-right" :style="sectionStyle(0.365, 0.60)">
            <span class="cl-marker">02 / ELEVATION</span>
            <h2>三级顶升</h2>
            <p>顶升轴向内套叠收拢，<br>再展开至 2.0M 最高工作位。</p>
            <p class="cl-card__readout">工作高度 <b>{{ telemetry.liftMeters.toFixed(2) }}</b> / 2.00 M</p>
          </section>

          <!-- 03 舵轮 -->
          <section class="cl-sec cl-card is-left is-low" :style="sectionStyle(0.645, 0.755)">
            <span class="cl-marker">03 / OMNIDRIVE</span>
            <h2>全向舵轮</h2>
            <p>四组减震舵轮独立转向，<br>狭窄工面原地换向，巡检不中断。</p>
          </section>

          <!-- 04 断层透察 -->
          <section class="cl-sec cl-card is-top" :style="sectionStyle(0.775, 0.915)">
            <span class="cl-marker">04 / X-RAY</span>
            <h2>断层透察</h2>
            <p>一条实体切片自下而上，逐站检定八大总成。</p>
          </section>

          <!-- 收束 CTA -->
          <section class="cl-sec cl-cta" :style="ctaStyle">
            <span class="cl-marker">05 / STANDBY</span>
            <h2>开始你的巡检。</h2>
            <div class="cl-cta__actions">
              <NuxtLink to="/" class="cl-btn is-primary">返回企业官网</NuxtLink>
              <button type="button" class="cl-btn" @click="backToTop">回到顶部</button>
            </div>
            <p class="cl-cta__foot">HENGVIEW — SUPERVISION UNIT / QQYH</p>
          </section>

          <!-- 部件引线锚点 -->
          <div class="cl-anchors" aria-hidden="true">
            <div
              v-for="anchor in focusAnchors"
              :key="anchor.code"
              class="cl-anchor"
              :style="anchorStyle(anchor)"
            >
              <i class="cl-anchor__dot" />
              <i class="cl-anchor__line" />
              <span class="cl-anchor__label"><b>{{ anchor.code }}</b>{{ anchor.label }}</span>
            </div>
            <div
              v-for="anchor in structureAnchors"
              :key="anchor.code"
              class="cl-anchor is-bom"
              :style="anchorStyle(anchor)"
            >
              <i class="cl-anchor__dot" />
              <i class="cl-anchor__line" />
              <span class="cl-anchor__label"><b>{{ anchor.code }}</b>{{ anchor.label }}</span>
            </div>
          </div>

          <HengviewScrollRail :progress="scrollProgress" :active="railActive" />
        </div>
      </div>
    </section>
  </main>
</template>

<style scoped>
.cl-page {
  --void: #05070A;
  --ink: #0A0E14;
  --line: rgb(198 216 228 / 10%);
  --line-strong: rgb(198 216 228 / 22%);
  --mist: #8A97A6;
  --ice: #E9F2F8;
  --cyan: #46D7EA;
  --ember: #FF7A3C;
  --fx-tick-strong: rgb(198 216 228 / 20%);
  --fx-tick-weak: rgb(198 216 228 / 8%);
  --mono: 'Chakra Petch', 'IBM Plex Mono', ui-monospace, 'Cascadia Mono', monospace;
  --cn: 'HarmonyOS Sans SC', 'PingFang SC', 'Microsoft YaHei UI', 'Instrument Sans', sans-serif;

  min-height: 100svh;
  background: var(--void);
  color: var(--ice);
}

/* 白厅主题：文字压暗、强调色加深，与 3D 场景的开灯过渡同步 */
.cl-page.is-light {
  --line: rgb(19 26 33 / 15%);
  --line-strong: rgb(19 26 33 / 30%);
  --mist: #4E5B66;
  --ice: #131A21;
  --cyan: #0B8CA0;
  --fx-tick-strong: rgb(19 26 33 / 28%);
  --fx-tick-weak: rgb(19 26 33 / 11%);
}

/* 主题切换的文字/线条颜色过渡 */
.cl-hud__logo,
.cl-hud__unit,
.cl-hud__data span,
.cl-hud__themeOpt,
.cl-hud__themeSlash,
.cl-marker,
.cl-card,
.cl-card h2,
.cl-card p,
.cl-card__readout,
.cl-hero h1,
.cl-hero__sub,
.cl-hero__spec,
.cl-anchor__label,
.cl-anchor__label b,
.cl-cta h2,
.cl-cta__foot,
.cl-frame i {
  transition: color 750ms ease, border-color 750ms ease;
}

.cl-track {
  position: relative;
  height: 720svh;
  background: var(--void);
}

.cl-stage {
  position: sticky;
  top: 0;
  height: 100svh;
  min-height: 540px;
  overflow: hidden;
  background: var(--void);
  isolation: isolate;
}

.cl-stage__scene {
  position: absolute;
  inset: 0;
  z-index: 0;
}

.cl-stage__scene :deep(canvas) {
  display: block;
  width: 100%;
  height: 100%;
}

/* ---------- 仪表边框 ---------- */
.cl-frame {
  position: absolute;
  inset: 18px;
  z-index: 2;
  pointer-events: none;
}

.cl-frame i {
  position: absolute;
  width: 14px;
  height: 14px;
  border: 0 solid var(--line-strong);
}

.cl-frame .is-tl { top: 0; left: 0; border-top-width: 1px; border-left-width: 1px; }
.cl-frame .is-tr { top: 0; right: 0; border-top-width: 1px; border-right-width: 1px; }
.cl-frame .is-bl { bottom: 0; left: 0; border-bottom-width: 1px; border-left-width: 1px; }
.cl-frame .is-br { right: 0; bottom: 0; border-bottom-width: 1px; border-right-width: 1px; }

/* ---------- 2D 动效层 ---------- */
.cl-fx {
  position: absolute;
  inset: 0;
  z-index: 2;
  overflow: hidden;
  opacity: 0;
  pointer-events: none;
  transition: opacity 900ms ease;
}

.cl-fx.is-live {
  opacity: 1;
}

/* 测距刻度尺：每 9px 细刻度、每 45px 长刻度，随滚动缓慢平移 */
.cl-fx__ruler {
  position: absolute;
  top: -20%;
  width: 9px;
  height: 150%;
  background:
    repeating-linear-gradient(
      to bottom,
      var(--fx-tick-strong) 0,
      var(--fx-tick-strong) 1px,
      transparent 1px,
      transparent 45px
    ),
    repeating-linear-gradient(
      to bottom,
      var(--fx-tick-weak) 0,
      var(--fx-tick-weak) 1px,
      transparent 1px,
      transparent 9px
    );
  background-size: 100% 100%, 55% 100%;
  background-repeat: no-repeat, no-repeat;
  background-position: left top, left top;
  will-change: transform;
}

.cl-fx__ruler.is-left {
  left: max(4vw, 30px);
}

.cl-fx__ruler.is-right {
  right: max(4vw, 30px);
  transform: scaleX(-1);
  background-position: right top, right top;
}

/* 周期扫描线：极淡的一次横向下行，约 9s 一轮 */
.cl-fx__sweep {
  position: absolute;
  right: 0;
  left: 0;
  height: 1px;
  background: linear-gradient(
    90deg,
    transparent,
    color-mix(in srgb, var(--cyan) 10%, transparent) 22%,
    color-mix(in srgb, var(--cyan) 10%, transparent) 78%,
    transparent
  );
  box-shadow: 0 0 14px color-mix(in srgb, var(--cyan) 7%, transparent);
  animation: cl-sweep 9.5s linear infinite;
}

@keyframes cl-sweep {
  0% {
    top: -2%;
    opacity: 0;
  }

  8% {
    opacity: 1;
  }

  86% {
    opacity: 1;
  }

  100% {
    top: 103%;
    opacity: 0;
  }
}

/* ---------- HUD ---------- */
.cl-hud {
  position: absolute;
  top: 30px;
  right: 42px;
  left: 42px;
  z-index: 4;
  display: flex;
  align-items: baseline;
  justify-content: space-between;
  gap: 20px;
  opacity: 0.35;
  transition: opacity 600ms ease;
}

.cl-hud.is-online {
  opacity: 1;
}

.cl-hud__brand {
  display: flex;
  align-items: baseline;
  gap: 14px;
}

.cl-hud__logo {
  font-family: 'Syne', var(--cn);
  font-size: 19px;
  font-weight: 600;
  letter-spacing: 0.02em;
}

.cl-hud__unit {
  color: var(--mist);
  font-family: var(--cn);
  font-size: 11px;
  font-weight: 400;
  letter-spacing: 0.14em;
}

.cl-hud__data {
  display: flex;
  gap: 26px;
  margin-left: auto;
  color: var(--mist);
  font-family: var(--mono);
  font-size: 11px;
  font-weight: 500;
  font-variant-numeric: tabular-nums;
  letter-spacing: 0.2em;
}

.cl-hud__data span:first-child {
  color: var(--cyan);
}

/* 日夜切换钮：仪表风格的 DARK / LIGHT 双态 */
.cl-hud__theme {
  display: flex;
  gap: 7px;
  align-items: baseline;
  margin-left: 6px;
  padding: 0;
  border: 0;
  background: none;
  font-family: var(--mono);
  font-size: 11px;
  font-weight: 500;
  letter-spacing: 0.2em;
  cursor: pointer;
  pointer-events: auto;
}

.cl-hud__themeOpt {
  position: relative;
  padding-bottom: 3px;
  border-bottom: 1px solid transparent;
  color: color-mix(in srgb, var(--mist) 55%, transparent);
  transition: color 400ms ease, border-color 400ms ease;
}

.cl-hud__themeOpt.is-on {
  border-bottom-color: var(--cyan);
  color: var(--cyan);
}

.cl-hud__theme:hover .cl-hud__themeOpt:not(.is-on),
.cl-hud__theme:focus-visible .cl-hud__themeOpt:not(.is-on) {
  color: var(--ice);
}

.cl-hud__theme:focus-visible {
  outline: 1px solid var(--cyan);
  outline-offset: 4px;
}

.cl-hud__themeSlash {
  color: color-mix(in srgb, var(--mist) 40%, transparent);
  font-style: normal;
}

/* ---------- 加载态 ---------- */
.cl-loader {
  position: absolute;
  inset: 0;
  z-index: 8;
  display: grid;
  grid-template-columns: minmax(0, min(320px, 56vw));
  align-content: center;
  justify-content: center;
  gap: 14px;
  background: var(--void);
}

.cl-loader__line {
  height: 1px;
  background: linear-gradient(90deg, transparent, var(--cyan) 18%, var(--cyan) 82%, transparent);
  box-shadow: 0 0 10px rgb(70 215 234 / 40%);
  transform: scaleX(0);
  transform-origin: center;
  transition: transform 120ms linear;
}

.cl-loader.is-collapsing .cl-loader__line {
  transform: scaleX(0.001) !important;
  transition: transform 520ms cubic-bezier(0.7, 0, 0.2, 1);
}

.cl-loader.is-collapsing .cl-loader__meta {
  opacity: 0;
  transition: opacity 320ms ease;
}

.cl-loader__meta {
  display: flex;
  justify-content: space-between;
  margin: 0;
  color: var(--mist);
  font-family: var(--mono);
  font-size: 10px;
  font-variant-numeric: tabular-nums;
  letter-spacing: 0.18em;
}

.cl-loader__meta b {
  color: var(--ice);
  font-weight: 500;
}

.cl-loader-leave-active {
  transition: opacity 700ms ease;
}

.cl-loader-leave-to {
  opacity: 0;
}

.cl-error {
  position: absolute;
  top: 50%;
  left: 50%;
  z-index: 9;
  margin: 0;
  color: var(--ember);
  font-family: var(--mono);
  font-size: 11px;
  letter-spacing: 0.14em;
  transform: translate(-50%, -50%);
}

/* ---------- 叙事层 ---------- */
.cl-ui {
  position: absolute;
  inset: 0;
  z-index: 3;
  pointer-events: none;
}

.cl-sec {
  position: absolute;
  filter: blur(var(--sec-blur, 0));
  opacity: var(--sec-opacity, 0);
  transform: translate3d(0, var(--sec-y, 0), 0);
  will-change: transform, opacity;
}

.cl-marker {
  display: block;
  color: var(--cyan);
  font-family: var(--mono);
  font-size: 11px;
  font-weight: 600;
  letter-spacing: 0.3em;
}

.cl-hero {
  bottom: 13%;
  left: 7vw;
}

/* 英雄文案错峰浮现：开机完成后逐行升起 */
.cl-hero .cl-marker,
.cl-hero h1,
.cl-hero .cl-hero__sub,
.cl-hero .cl-hero__spec {
  opacity: 0;
  filter: blur(7px);
  transform: translateY(30px);
  transition:
    opacity 950ms cubic-bezier(0.22, 1, 0.36, 1),
    transform 950ms cubic-bezier(0.22, 1, 0.36, 1),
    filter 950ms cubic-bezier(0.22, 1, 0.36, 1);
  will-change: transform, opacity, filter;
}

.cl-hero.is-revealed .cl-marker,
.cl-hero.is-revealed h1,
.cl-hero.is-revealed .cl-hero__sub,
.cl-hero.is-revealed .cl-hero__spec {
  opacity: 1;
  filter: blur(0);
  transform: translateY(0);
}

.cl-hero.is-revealed h1 {
  transition-delay: 130ms;
}

.cl-hero.is-revealed .cl-hero__sub {
  transition-delay: 290ms;
}

.cl-hero.is-revealed .cl-hero__spec {
  transition-delay: 410ms;
}

.cl-hero h1 {
  margin: 16px 0 0;
  font-family: 'Syne', var(--cn);
  font-size: clamp(52px, 7.4vw, 108px);
  font-weight: 600;
  letter-spacing: 0.01em;
  line-height: 0.94;
}

.cl-hero h1 i {
  color: var(--cyan);
  font-style: normal;
}

.cl-hero__sub {
  margin: 18px 0 0;
  color: var(--ice);
  font-family: var(--cn);
  font-size: 15px;
  font-weight: 300;
  letter-spacing: 0.04em;
}

.cl-hero__spec {
  margin: 10px 0 0;
  color: var(--mist);
  font-family: var(--mono);
  font-size: 10px;
  letter-spacing: 0.2em;
}

.cl-card {
  width: min(360px, 40vw);
  padding-left: 18px;
  border-left: 1px solid color-mix(in srgb, var(--cyan) 45%, transparent);
}

.cl-card.is-left {
  top: 24%;
  left: 7vw;
}

.cl-card.is-right {
  top: 26%;
  right: 7vw;
}

.cl-card.is-low {
  top: auto;
  bottom: 20%;
}

.cl-card.is-top {
  top: 12%;
  left: 50%;
  width: min(420px, 60vw);
  padding-left: 0;
  border-left: 0;
  text-align: center;
  transform: translate3d(-50%, var(--sec-y, 0), 0);
}

.cl-card h2 {
  margin: 12px 0 0;
  font-family: var(--cn);
  font-size: clamp(30px, 3.2vw, 42px);
  font-weight: 300;
  letter-spacing: 0.02em;
  line-height: 1.1;
}

.cl-card p {
  margin: 12px 0 0;
  color: var(--mist);
  font-family: var(--cn);
  font-size: 15px;
  font-weight: 300;
  line-height: 1.6;
}

.cl-card__readout {
  color: var(--ice) !important;
  font-family: var(--mono) !important;
  font-size: 11px !important;
  font-variant-numeric: tabular-nums;
  letter-spacing: 0.16em;
}

.cl-card__readout b {
  color: var(--cyan);
  font-weight: 500;
}

/* ---------- CTA ---------- */
.cl-cta {
  top: 50%;
  left: 50%;
  text-align: center;
  transform: translate3d(-50%, calc(-50% + var(--sec-y, 0)), 0);
}

.cl-cta h2 {
  margin: 16px 0 0;
  font-family: var(--cn);
  font-size: clamp(34px, 4.4vw, 56px);
  font-weight: 300;
  letter-spacing: 0.04em;
}

.cl-cta__actions {
  display: flex;
  gap: 14px;
  justify-content: center;
  margin-top: 34px;
  pointer-events: auto;
}

.cl-btn {
  display: inline-block;
  padding: 13px 26px;
  border: 1px solid rgb(198 216 228 / 25%);
  border-radius: 2px;
  background: transparent;
  color: var(--ice);
  font-family: var(--cn);
  font-size: 13px;
  letter-spacing: 0.12em;
  cursor: pointer;
  transition: border-color 200ms ease, color 200ms ease, background-color 200ms ease, transform 200ms ease;
}

.cl-btn.is-primary {
  border-color: rgb(70 215 234 / 65%);
  color: var(--cyan);
}

.cl-btn:hover,
.cl-btn:focus-visible {
  border-color: var(--ember);
  color: var(--ember);
  outline: none;
  transform: translateY(-2px);
}

.cl-cta__foot {
  margin: 40px 0 0;
  color: rgb(138 151 166 / 55%);
  font-family: var(--mono);
  font-size: 9px;
  letter-spacing: 0.24em;
}

/* ---------- 锚点引线 ---------- */
.cl-anchors {
  position: absolute;
  inset: 0;
}

.cl-anchor {
  position: absolute;
  width: 0;
  height: 0;
}

.cl-anchor__dot {
  position: absolute;
  top: -2px;
  left: -2px;
  width: 4px;
  height: 4px;
  border-radius: 50%;
  background: var(--cyan);
  box-shadow: 0 0 8px rgb(70 215 234 / 60%);
}

.cl-anchor__line {
  position: absolute;
  top: 0;
  left: 0;
  width: 46px;
  height: 1px;
  background: linear-gradient(
    90deg,
    color-mix(in srgb, var(--cyan) 70%, transparent),
    color-mix(in srgb, var(--cyan) 12%, transparent)
  );
  transform: scaleX(var(--flip, 1));
  transform-origin: left center;
}

.cl-anchor__label {
  position: absolute;
  top: -7px;
  left: 54px;
  display: flex;
  gap: 8px;
  align-items: baseline;
  color: var(--ice);
  font-family: var(--cn);
  font-size: 11px;
  letter-spacing: 0.1em;
  white-space: nowrap;
}

.cl-anchor[style*='--flip: -1'] .cl-anchor__label {
  right: 54px;
  left: auto;
}

.cl-anchor__label b {
  color: var(--mist);
  font-family: var(--mono);
  font-size: 9px;
  font-weight: 500;
  letter-spacing: 0.12em;
}

.cl-anchor.is-bom .cl-anchor__dot {
  background: var(--ice);
  box-shadow: 0 0 6px rgb(233 242 248 / 50%);
}

/* ---------- 响应式 ---------- */
@media (max-width: 700px) {
  .cl-track {
    height: 660svh;
  }

  .cl-stage {
    min-height: 500px;
  }

  .cl-hud {
    top: 20px;
    right: 18px;
    left: 18px;
    display: grid;
    grid-template-areas:
      'brand theme'
      'data data';
    grid-template-columns: minmax(0, 1fr) auto;
    align-items: center;
    gap: 10px 12px;
  }

  .cl-hud__brand {
    grid-area: brand;
    min-width: 0;
  }

  .cl-hud__unit {
    display: none;
  }

  .cl-hud__data {
    display: grid;
    grid-area: data;
    grid-template-columns: repeat(3, minmax(0, 1fr));
    gap: 8px;
    width: 100%;
    margin-left: 0;
    font-size: 9px;
    letter-spacing: 0.12em;
  }

  .cl-hud__data span {
    min-width: 0;
    white-space: nowrap;
  }

  .cl-hud__data span:nth-child(2) {
    text-align: center;
  }

  .cl-hud__data span:last-child {
    text-align: right;
  }

  .cl-hud__theme {
    grid-area: theme;
    justify-self: end;
    margin-left: 0;
    font-size: 9px;
    letter-spacing: 0.12em;
    white-space: nowrap;
  }

  .cl-frame {
    inset: 10px;
  }

  .cl-hero {
    bottom: 15%;
    left: 18px;
  }

  .cl-hero h1 {
    font-size: 15vw;
  }

  .cl-card {
    width: min(320px, 82vw);
  }

  .cl-card.is-left {
    top: 17%;
    left: 18px;
  }

  .cl-card.is-right {
    top: 15%;
    right: 18px;
  }

  .cl-card.is-low {
    bottom: 22%;
  }

  .cl-card.is-top {
    top: 14%;
    width: 84vw;
  }

  .cl-card h2 {
    font-size: 28px;
  }

  .cl-card p {
    font-size: 13px;
  }

  .cl-anchor__label {
    font-size: 10px;
  }

  .cl-cta__actions {
    flex-direction: column;
    align-items: center;
  }
}

@media (prefers-reduced-motion: reduce) {
  .cl-sec {
    filter: none !important;
    transition: opacity 300ms linear;
  }

  .cl-hero .cl-marker,
  .cl-hero h1,
  .cl-hero .cl-hero__sub,
  .cl-hero .cl-hero__spec {
    filter: none;
    opacity: 1;
    transform: none;
    transition: none;
  }

  .cl-fx__sweep {
    animation: none;
    opacity: 0;
  }

  .cl-btn {
    transition: none;
  }
}
</style>
