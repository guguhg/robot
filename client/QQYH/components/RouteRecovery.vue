<script setup lang="ts">
const props = defineProps<{
  statusCode: number
}>()

const isNotFound = computed(() => props.statusCode === 404)
const signalCode = computed(() => `ERR-${String(props.statusCode).padStart(3, '0')}`)
const eyebrow = computed(() => (isNotFound.value ? 'ROUTE / UNRESOLVED' : 'SYSTEM / INTERRUPTION'))
const heading = computed(() => (isNotFound.value ? '404' : '连接中断'))
const detail = computed(() => (
  isNotFound.value
    ? '该坐标不在当前路径图中。'
    : '当前路径暂时无法恢复。'
))

const retry = () => {
  if (import.meta.client) window.location.reload()
}
</script>

<template>
  <main class="not-found" :class="{ 'not-found--system': !isNotFound }" aria-labelledby="not-found-title">
    <div class="not-found__grid" aria-hidden="true">
      <span v-for="column in 7" :key="`column-${column}`" class="not-found__grid-line not-found__grid-line--vertical" :style="{ '--index': column }" />
      <span v-for="row in 6" :key="`row-${row}`" class="not-found__grid-line not-found__grid-line--horizontal" :style="{ '--index': row }" />
    </div>

    <header class="not-found__header" aria-label="系统状态">
      <span class="not-found__brand">ZERO</span>
      <span class="not-found__status"><i /> {{ signalCode }}</span>
    </header>

    <section class="not-found__stage">
      <div class="not-found__numeral" aria-hidden="true">{{ props.statusCode }}</div>

      <div class="not-found__locator" aria-hidden="true">
        <span class="not-found__locator-frame not-found__locator-frame--outer" />
        <span class="not-found__locator-frame not-found__locator-frame--inner" />
        <span class="not-found__locator-axis not-found__locator-axis--x" />
        <span class="not-found__locator-axis not-found__locator-axis--y" />
        <span class="not-found__locator-scan" />
        <span class="not-found__locator-point" />
        <span class="not-found__locator-corner not-found__locator-corner--tl" />
        <span class="not-found__locator-corner not-found__locator-corner--tr" />
        <span class="not-found__locator-corner not-found__locator-corner--bl" />
        <span class="not-found__locator-corner not-found__locator-corner--br" />
      </div>

      <div class="not-found__copy">
        <p class="not-found__eyebrow">{{ eyebrow }}</p>
        <h1 id="not-found-title">{{ heading }}</h1>
        <p class="not-found__detail">{{ detail }}</p>

        <div class="not-found__actions" aria-label="错误页操作">
          <a href="/" class="not-found__action not-found__action--primary">返回首页</a>
          <button type="button" class="not-found__action not-found__action--secondary" @click="retry">重新定位</button>
        </div>
      </div>
    </section>

    <footer class="not-found__footer">
      <span>LAST KNOWN POINT / ZERO</span>
      <span>FIELD NAVIGATION</span>
    </footer>
  </main>
</template>

<style scoped>
.not-found {
  --ink: #0d1112;
  --surface: #151b1c;
  --line: rgb(233 241 237 / 17%);
  --muted: #a5b1ad;
  --light: #f0f5f1;
  --signal: #d8ff42;
  --alert: #f2674f;
  position: relative;
  display: grid;
  min-height: 100svh;
  overflow: hidden;
  background: var(--ink);
  color: var(--light);
  isolation: isolate;
}

.not-found::before,
.not-found::after {
  position: absolute;
  z-index: -1;
  content: '';
  pointer-events: none;
}

.not-found::before {
  top: 8svh;
  right: 6vw;
  bottom: 8svh;
  left: 6vw;
  border: 1px solid var(--line);
}

.not-found::after {
  top: 50%;
  left: 50%;
  width: min(72vw, 960px);
  aspect-ratio: 1;
  border: 1px solid rgb(216 255 66 / 12%);
  transform: translate(-50%, -50%) rotate(45deg);
}

.not-found__grid {
  position: absolute;
  z-index: -2;
  inset: 0;
  overflow: hidden;
  pointer-events: none;
}

.not-found__grid-line {
  position: absolute;
  display: block;
  background: var(--line);
}

.not-found__grid-line--vertical {
  top: 0;
  bottom: 0;
  left: calc(var(--index) * 12.5%);
  width: 1px;
}

.not-found__grid-line--horizontal {
  right: 0;
  left: 0;
  top: calc(var(--index) * 14.2857%);
  height: 1px;
}

.not-found__header,
.not-found__footer {
  position: relative;
  z-index: 2;
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 16px;
  padding-right: clamp(22px, 4vw, 58px);
  padding-left: clamp(22px, 4vw, 58px);
  font-family: 'Instrument Sans', 'Microsoft YaHei UI', sans-serif;
  font-size: 11px;
  font-weight: 580;
  line-height: 1;
}

.not-found__header {
  height: clamp(66px, 10svh, 96px);
}

.not-found__brand {
  font-family: 'Syne', 'Instrument Sans', sans-serif;
  font-size: clamp(21px, 2vw, 29px);
  font-weight: 700;
}

.not-found__status {
  display: inline-flex;
  align-items: center;
  gap: 9px;
  color: var(--muted);
}

.not-found__status i {
  width: 7px;
  height: 7px;
  background: var(--alert);
  box-shadow: 0 0 16px rgb(242 103 79 / 48%);
  animation: not-found-status 1.9s steps(2, end) infinite;
}

.not-found__stage {
  position: relative;
  display: grid;
  align-items: center;
  min-height: 0;
  padding: clamp(28px, 5vw, 76px);
}

.not-found__numeral {
  position: absolute;
  top: 50%;
  left: 50%;
  color: rgb(240 245 241 / 7%);
  font-family: 'Syne', 'Instrument Sans', sans-serif;
  font-size: clamp(190px, 35vw, 620px);
  font-weight: 680;
  line-height: 0.72;
  pointer-events: none;
  transform: translate(-50%, -49%);
  user-select: none;
}

.not-found__locator {
  position: absolute;
  top: 50%;
  left: 57%;
  width: clamp(172px, 21vw, 298px);
  aspect-ratio: 1;
  pointer-events: none;
  transform: translate(-50%, -50%);
}

.not-found__locator-frame,
.not-found__locator-axis,
.not-found__locator-scan,
.not-found__locator-point,
.not-found__locator-corner {
  position: absolute;
  display: block;
}

.not-found__locator-frame--outer {
  inset: 0;
  border: 1px solid rgb(240 245 241 / 44%);
  transform: rotate(45deg);
  animation: not-found-frame 12s linear infinite;
}

.not-found__locator-frame--inner {
  inset: 20%;
  border: 1px solid rgb(216 255 66 / 72%);
  transform: rotate(45deg);
}

.not-found__locator-axis--x {
  top: 50%;
  left: -21%;
  width: 142%;
  height: 1px;
  background: rgb(240 245 241 / 32%);
}

.not-found__locator-axis--y {
  top: -21%;
  left: 50%;
  width: 1px;
  height: 142%;
  background: rgb(240 245 241 / 32%);
}

.not-found__locator-scan {
  top: 50%;
  left: -12%;
  width: 124%;
  height: 2px;
  background: var(--signal);
  box-shadow: 0 0 22px rgb(216 255 66 / 40%);
  animation: not-found-scan 2.7s cubic-bezier(0.77, 0, 0.18, 1) infinite;
  transform-origin: left center;
}

.not-found__locator-point {
  top: 50%;
  left: 50%;
  width: 12px;
  height: 12px;
  border: 2px solid var(--signal);
  background: var(--ink);
  transform: translate(-50%, -50%) rotate(45deg);
}

.not-found__locator-corner {
  width: 22px;
  height: 22px;
}

.not-found__locator-corner--tl {
  top: 0;
  left: 0;
  border-top: 2px solid var(--signal);
  border-left: 2px solid var(--signal);
}

.not-found__locator-corner--tr {
  top: 0;
  right: 0;
  border-top: 2px solid var(--signal);
  border-right: 2px solid var(--signal);
}

.not-found__locator-corner--bl {
  bottom: 0;
  left: 0;
  border-bottom: 2px solid var(--signal);
  border-left: 2px solid var(--signal);
}

.not-found__locator-corner--br {
  right: 0;
  bottom: 0;
  border-right: 2px solid var(--signal);
  border-bottom: 2px solid var(--signal);
}

.not-found__copy {
  position: relative;
  z-index: 1;
  display: grid;
  align-self: center;
  gap: 18px;
  width: min(100%, 480px);
  margin-left: clamp(8px, 10vw, 180px);
}

.not-found__eyebrow,
.not-found__detail {
  margin: 0;
  font-family: 'Instrument Sans', 'Microsoft YaHei UI', sans-serif;
  font-size: 12px;
  font-weight: 580;
}

.not-found__eyebrow {
  color: var(--signal);
}

.not-found__copy h1 {
  margin: 0;
  font-family: 'Syne', 'Instrument Sans', sans-serif;
  font-size: clamp(61px, 7.4vw, 132px);
  font-weight: 680;
  line-height: 0.76;
}

.not-found__detail {
  max-width: 24ch;
  color: var(--muted);
  font-size: 15px;
  line-height: 1.6;
}

.not-found__actions {
  display: flex;
  flex-wrap: wrap;
  gap: 10px;
  margin-top: 8px;
}

.not-found__action {
  display: inline-flex;
  align-items: center;
  justify-content: center;
  min-height: 44px;
  padding: 0 18px;
  border: 1px solid transparent;
  border-radius: 0;
  font: inherit;
  font-size: 12px;
  font-weight: 650;
  letter-spacing: 0;
  cursor: pointer;
  transition: background-color 180ms ease, border-color 180ms ease, color 180ms ease, transform 180ms ease;
}

.not-found__action:focus-visible {
  outline: 2px solid var(--light);
  outline-offset: 3px;
}

.not-found__action:hover {
  transform: translateY(-2px);
}

.not-found__action--primary {
  background: var(--signal);
  color: var(--ink);
}

.not-found__action--primary:hover {
  background: var(--light);
}

.not-found__action--secondary {
  border-color: rgb(240 245 241 / 38%);
  background: transparent;
  color: var(--light);
}

.not-found__action--secondary:hover {
  border-color: var(--signal);
  color: var(--signal);
}

.not-found__footer {
  height: clamp(56px, 8svh, 78px);
  border-top: 1px solid var(--line);
  color: var(--muted);
}

.not-found__footer span:last-child {
  color: rgb(240 245 241 / 38%);
}

.not-found--system {
  --signal: #8dd7ff;
  --alert: #f2674f;
}

@keyframes not-found-status {
  0%,
  100% {
    opacity: 1;
  }
  50% {
    opacity: 0.18;
  }
}

@keyframes not-found-frame {
  to {
    transform: rotate(405deg);
  }
}

@keyframes not-found-scan {
  0%,
  100% {
    opacity: 0.1;
    transform: scaleX(0.2);
  }
  45% {
    opacity: 1;
    transform: scaleX(1);
  }
  60% {
    opacity: 0.22;
    transform: scaleX(1);
  }
}

@media (max-width: 760px) {
  .not-found::before {
    top: 74px;
    right: 18px;
    bottom: 74px;
    left: 18px;
  }

  .not-found::after {
    width: 118vw;
  }

  .not-found__grid-line--vertical {
    left: calc(var(--index) * 16.6667%);
  }

  .not-found__grid-line--vertical:nth-of-type(6),
  .not-found__grid-line--vertical:nth-of-type(7) {
    display: none;
  }

  .not-found__header,
  .not-found__footer {
    padding-right: 22px;
    padding-left: 22px;
    font-size: 10px;
  }

  .not-found__stage {
    align-items: end;
    padding: 22px;
  }

  .not-found__numeral {
    top: 29%;
    font-size: min(73vw, 310px);
  }

  .not-found__locator {
    top: 31%;
    left: 59%;
    width: min(52vw, 240px);
  }

  .not-found__copy {
    gap: 14px;
    width: min(100%, 360px);
    margin: 0 0 clamp(28px, 7svh, 64px);
  }

  .not-found__copy h1 {
    max-inline-size: 100%;
    font-size: clamp(56px, 16.5vw, 70px);
    line-height: 0.9;
    overflow-wrap: anywhere;
  }

  .not-found__detail {
    font-size: 14px;
  }

  .not-found__actions {
    width: 100%;
  }

  .not-found__action {
    flex: 1 1 140px;
  }
}

@media (prefers-reduced-motion: reduce) {
  .not-found__status i,
  .not-found__locator-frame--outer,
  .not-found__locator-scan {
    animation: none;
  }
}
</style>
