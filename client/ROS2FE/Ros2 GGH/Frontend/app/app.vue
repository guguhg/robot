<script setup lang="ts">
import { gsap } from 'gsap'
import { Motion } from 'motion-v'
import ConsoleView from '@/components/ConsoleView.vue'
import OverviewView from '@/components/OverviewView.vue'

type ViewKey = 'overview' | 'console'
type ConsoleStage = 'video' | 'map' | 'cloud'

const activeView = ref<ViewKey>('overview')
const authOpen = ref(false)
const guestBusy = ref(false)
const requestedConsoleStage = useState<ConsoleStage>('console:requested-stage', () => 'video')
const pendingConsoleStage = ref<ConsoleStage | null>(null)

const {
  session,
  authNotice,
  loginAsGuest,
  logout,
  startExpiryGuard,
} = useAuth()

const views: Array<{ key: ViewKey; label: string }> = [
  { key: 'overview', label: '概览' },
  { key: 'console', label: '控制台' },
]
const activePage = computed(() => activeView.value === 'console' ? ConsoleView : OverviewView)

/* 进入控制台后收起站点 chrome（Design/console-hud.md v3 §6） */
const consoleMode = ref(false)
const chromeLock = ref(false)

watch(activeView, (v) => {
  const reduced = window.matchMedia('(prefers-reduced-motion: reduce)').matches

  if (v === 'console') {
    chromeLock.value = true
    if (reduced) {
      consoleMode.value = true
      return
    }
    gsap
      .timeline({ onComplete: () => (consoleMode.value = true) })
      .to('.site-nav', { yPercent: -100, duration: 0.32, ease: 'power2.in' }, 0)
      .to('.site-footer', { yPercent: 100, duration: 0.32, ease: 'power2.in' }, 0)
  } else {
    consoleMode.value = false
    if (reduced) {
      gsap.set(['.site-nav', '.site-footer'], { clearProps: 'transform' })
      chromeLock.value = false
      return
    }
    gsap.to(['.site-nav', '.site-footer'], {
      yPercent: 0,
      duration: 0.28,
      ease: 'power3.out',
      delay: 0.05,
      clearProps: 'transform',
      onComplete: () => (chromeLock.value = false),
    })
  }
})

function onKeydown(e: KeyboardEvent) {
  if (e.key === 'Escape' && activeView.value === 'console') activeView.value = 'overview'
}

async function enterAsGuest() {
  if (guestBusy.value) return
  guestBusy.value = true
  try {
    await loginAsGuest()
  } catch {
    authNotice.value = '游客登录失败，请检查后端连接后重试。'
    authOpen.value = true
  } finally {
    guestBusy.value = false
  }
}

function signOut() {
  logout()
  activeView.value = 'overview'
}

function selectNavView(view: ViewKey) {
  if (view === 'console') {
    openConsole()
    return
  }
  activeView.value = 'overview'
}

function openConsole(stage: ConsoleStage = 'video') {
  requestedConsoleStage.value = stage
  if (!session.value) {
    pendingConsoleStage.value = stage
    authOpen.value = true
    return
  }
  activeView.value = 'console'
}

watch(authNotice, (notice) => {
  if (notice) authOpen.value = true
})

watch(session, (next) => {
  if (!next && activeView.value === 'console') activeView.value = 'overview'
  if (next && pendingConsoleStage.value) {
    requestedConsoleStage.value = pendingConsoleStage.value
    pendingConsoleStage.value = null
    authOpen.value = false
    activeView.value = 'console'
  }
})

let mm: gsap.MatchMedia | undefined
let stopExpiryGuard: (() => void) | undefined

onMounted(() => {
  window.addEventListener('keydown', onKeydown)
  stopExpiryGuard = startExpiryGuard()

  mm = gsap.matchMedia()
  mm.add('(prefers-reduced-motion: no-preference)', () => {
    const tl = gsap.timeline({ defaults: { ease: 'power3.out', duration: 0.55 } })
    tl.from('.site-nav', { yPercent: -100, duration: 0.45, ease: 'power2.out', clearProps: 'all' })
      .from(
        ['.nav-left > *', '.nav-right > *'],
        { y: -8, autoAlpha: 0, stagger: 0.05, clearProps: 'all' },
        '-=0.15',
      )
      .from('.site-footer', { autoAlpha: 0, duration: 0.4, clearProps: 'all' }, '-=0.25')
  })
})

onUnmounted(() => {
  window.removeEventListener('keydown', onKeydown)
  stopExpiryGuard?.()
  mm?.revert()
})
</script>

<template>
  <div class="page" :class="{ 'console-mode': consoleMode, 'chrome-lock': chromeLock }">
    <header class="site-nav">
      <nav class="nav-inner" aria-label="主导航">
        <div class="nav-left">
          <a class="brand" href="#" @click.prevent="activeView = 'overview'">
            <svg class="brand-mark" viewBox="0 0 24 24" aria-hidden="true">
              <rect x="1" y="1" width="22" height="22" rx="6.5" fill="currentColor" />
              <circle cx="12" cy="12" r="3" fill="#fff" />
              <path
                d="M12 5.2a6.8 6.8 0 0 1 6.8 6.8"
                fill="none"
                stroke="var(--primary)"
                stroke-width="2"
                stroke-linecap="round"
              />
            </svg>
            <span class="brand-name">Mio</span>
          </a>

          <span class="nav-divider" aria-hidden="true"></span>

          <ul class="nav-links">
            <li v-for="view in views" :key="view.key">
              <a
                href="#"
                :aria-current="activeView === view.key ? 'page' : undefined"
                @click.prevent="selectNavView(view.key)"
              >
                {{ view.label }}
              </a>
            </li>
          </ul>

          <button class="nav-console-mobile" type="button" aria-label="进入控制台" @click="openConsole()">
            <Icon name="lucide:monitor-up" size="16" aria-hidden="true" />
          </button>
        </div>

        <div class="nav-right">
          <template v-if="session">
            <span class="nav-account" :title="session.email || session.username">
              <span class="nav-account-icon" aria-hidden="true">
                <Icon
                  :name="session.isGuest ? 'lucide:eye' : session.permissions.includes('system.admin') ? 'lucide:shield-check' : 'lucide:user-round'"
                  size="15"
                />
              </span>
              <span class="nav-account-copy">
                <b>{{ session.username }}</b>
                <small>{{ session.isGuest ? '游客' : session.permissions.includes('system.admin') ? '管理员' : '成员' }}</small>
              </span>
            </span>
            <span class="nav-divider" aria-hidden="true"></span>
            <button class="btn btn-ghost" type="button" @click="signOut">
              <Icon name="lucide:log-out" size="15" />
              <span>退出</span>
            </button>
          </template>

          <template v-else>
            <button class="btn btn-ghost nav-guest" type="button" :disabled="guestBusy" @click="enterAsGuest">
              <Icon :class="{ 'nav-spin': guestBusy }" :name="guestBusy ? 'lucide:loader-circle' : 'lucide:eye'" size="15" />
              <span>{{ guestBusy ? '连接中' : '游客访问' }}</span>
            </button>
            <span class="nav-divider" aria-hidden="true"></span>
            <Motion
              as="button"
              type="button"
              class="btn btn-primary"
              :while-hover="{ scale: 1.03 }"
              :while-press="{ scale: 0.96 }"
              :transition="{ type: 'spring', stiffness: 500, damping: 30 }"
              @click="authOpen = true"
            >
              <Icon name="lucide:log-in" size="15" />
              <span>登录</span>
            </Motion>
          </template>
        </div>
      </nav>
    </header>

    <div class="zero">
      <Transition name="view" mode="out-in">
        <!-- 控制台含有 GPU / RTC / SignalR 资源；视图切换仅停用并缓存，避免同一帧集中销毁。 -->
        <KeepAlive>
          <component
            :is="activePage"
            @exit="activeView = 'overview'"
            @enter-console="openConsole"
          />
        </KeepAlive>
      </Transition>
    </div>

    <footer class="site-footer" id="site-footer"></footer>

    <AuthDialog v-model="authOpen" />
  </div>
</template>

<style>
.page {
  display: flex;
  flex-direction: column;
  min-height: 100dvh;
}

/* 控制台沉浸态：chrome 收回后移出布局，.zero 占满视口 */
.page.chrome-lock {
  height: 100dvh;
  overflow: hidden;
}

.page.console-mode .site-nav,
.page.console-mode .site-footer {
  display: none;
}

/* ---------- 顶部导航 ---------- */

.site-nav {
  position: sticky;
  top: 0;
  z-index: 50;
  height: var(--nav-h);
  background: rgba(255, 255, 255, 0.86);
  backdrop-filter: saturate(180%) blur(10px);
  border-bottom: 1px solid var(--border);
}

.nav-inner {
  box-sizing: border-box;
  width: min(1440px, 100%);
  height: 100%;
  margin: 0 auto;
  padding: 0 24px;
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 24px;
}

.nav-left {
  display: flex;
  align-items: center;
  gap: 20px;
  min-width: 0;
}

.brand {
  display: flex;
  align-items: center;
  gap: 10px;
  color: var(--foreground);
  text-decoration: none;
  font-size: 15px;
  font-weight: 600;
  letter-spacing: 0.01em;
  white-space: nowrap;
}

.brand-mark {
  width: 24px;
  height: 24px;
  flex: none;
}

.nav-divider {
  width: 1px;
  height: 16px;
  background: var(--border);
  flex: none;
}

.nav-links {
  display: flex;
  align-items: center;
  gap: 4px;
  margin: 0;
  padding: 0;
  list-style: none;
}

.nav-console-mobile {
  display: none;
  align-items: center;
  justify-content: center;
  width: 32px;
  height: 32px;
  border: 1px solid var(--border);
  border-radius: 6px;
  background: transparent;
  color: var(--muted-foreground);
  cursor: pointer;
  transition: color 0.15s ease, background-color 0.15s ease, border-color 0.15s ease;
}

.nav-console-mobile:hover,
.nav-console-mobile:focus-visible {
  border-color: color-mix(in srgb, var(--primary) 36%, var(--border));
  background: color-mix(in srgb, var(--primary) 5%, #ffffff);
  color: var(--primary);
}

.nav-links a {
  display: block;
  padding: 6px 10px;
  border-radius: 6px;
  color: var(--muted-foreground);
  text-decoration: none;
  white-space: nowrap;
  transition:
    color 0.15s ease,
    background-color 0.15s ease;
}

.nav-links a:hover {
  color: var(--foreground);
  background: var(--muted);
}

.nav-links a[aria-current="page"] {
  color: var(--foreground);
  font-weight: 600;
}

.nav-right {
  display: flex;
  align-items: center;
  gap: 12px;
}

.btn {
  display: inline-flex;
  align-items: center;
  justify-content: center;
  gap: 7px;
  height: 34px;
  padding: 0 14px;
  border: 0;
  border-radius: var(--radius);
  background: transparent;
  cursor: pointer;
  font-size: 14px;
  font-family: inherit;
  text-decoration: none;
  white-space: nowrap;
  transition:
    color 0.15s ease,
    background-color 0.15s ease;
}

.btn:disabled {
  cursor: wait;
  opacity: 0.6;
}

.btn-ghost {
  color: var(--muted-foreground);
}

.btn-ghost:hover {
  color: var(--foreground);
  background: var(--muted);
}

.btn-primary {
  background: var(--primary);
  color: var(--primary-foreground);
  font-weight: 500;
}

.btn-primary:hover {
  background: var(--primary-strong);
}

.nav-guest {
  min-width: 100px;
}

.nav-account {
  display: flex;
  align-items: center;
  gap: 8px;
  min-width: 0;
}

.nav-account-icon {
  display: inline-flex;
  align-items: center;
  justify-content: center;
  width: 28px;
  height: 28px;
  flex: none;
  border-radius: 50%;
  background: var(--muted);
  color: var(--foreground);
}

.nav-account-copy {
  display: flex;
  flex-direction: column;
  min-width: 0;
  line-height: 1.2;
}

.nav-account-copy b {
  max-width: 132px;
  overflow: hidden;
  color: var(--foreground);
  font-size: 12px;
  font-weight: 600;
  text-overflow: ellipsis;
  white-space: nowrap;
}

.nav-account-copy small {
  color: var(--muted-foreground);
  font-size: 9px;
}

.nav-spin {
  animation: nav-spin 0.8s linear infinite;
}

@keyframes nav-spin {
  to { transform: rotate(360deg); }
}

/* ---------- 内容区 ---------- */

.zero {
  flex: 1;
  display: flex;
  flex-direction: column;
}

.view-enter-active,
.view-leave-active {
  transition:
    opacity 0.18s ease,
    transform 0.18s ease;
}

.view-enter-from,
.view-leave-to {
  opacity: 0;
  transform: translateY(6px);
}

/* ---------- 空页脚不再占用概览首屏，控制台沉浸态仍由视图类隐藏 ---------- */

.site-footer {
  flex: none;
  min-height: 0;
}

/* ---------- 窄屏 ---------- */

@media (max-width: 720px) {
  .nav-links,
  .nav-left .nav-divider {
    display: none;
  }

  .nav-console-mobile {
    display: inline-flex;
  }

  .nav-inner {
    padding: 0 16px;
  }

  .nav-account-copy small {
    display: none;
  }

  .nav-account-copy b {
    max-width: 88px;
  }

  .nav-right {
    gap: 8px;
  }

  .nav-right .btn {
    padding-inline: 10px;
  }
}

@media (prefers-reduced-motion: reduce) {
  .nav-spin {
    animation: none;
  }
}
</style>
