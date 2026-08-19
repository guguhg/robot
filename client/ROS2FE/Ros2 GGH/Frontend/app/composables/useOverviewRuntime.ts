import type { AuthSession } from '@/composables/useAuth'
import type { LinkState } from '@/composables/useRobot'

export const OVERVIEW_RUNTIME_MAX_AGE_MS = 30 * 60 * 1000
export const OVERVIEW_RUNTIME_PERSIST_INTERVAL_MS = 1000

const STORAGE_KEY = 'zerorobot:overview-runtime'

let pendingSnapshot: OverviewRuntimeSnapshot | null = null
let persistTimer: ReturnType<typeof setTimeout> | null = null
let lastPersistedAt = 0

export const OVERVIEW_LINK_KEYS = ['API', 'CMD', 'MAP', 'VID', 'PCD'] as const
export type OverviewLinkKey = typeof OVERVIEW_LINK_KEYS[number]

export interface OverviewRuntimeSnapshot {
  scope: string
  updatedAt: number
  batteryPercent: number | null
  batteryVoltage: number | null
  links: Record<OverviewLinkKey, LinkState>
}

export interface OverviewRuntimeUpdate {
  batteryPercent?: number | null
  batteryVoltage?: number | null
  links?: Partial<Record<OverviewLinkKey, LinkState>>
}

function emptyLinks(): Record<OverviewLinkKey, LinkState> {
  return { API: 'off', CMD: 'off', MAP: 'off', VID: 'off', PCD: 'off' }
}

function emptySnapshot(scope = ''): OverviewRuntimeSnapshot {
  return {
    scope,
    updatedAt: 0,
    batteryPercent: null,
    batteryVoltage: null,
    links: emptyLinks(),
  }
}

function isLinkState(value: unknown): value is LinkState {
  return value === 'ok' || value === 'connecting' || value === 'off'
}

function parseSnapshot(value: unknown, scope: string): OverviewRuntimeSnapshot | null {
  if (!value || typeof value !== 'object') return null
  const record = value as Partial<OverviewRuntimeSnapshot>
  if (
    record.scope !== scope
    || !Number.isFinite(record.updatedAt)
    || !record.updatedAt
    || Date.now() - record.updatedAt > OVERVIEW_RUNTIME_MAX_AGE_MS
    || !record.links
  ) return null

  const links = emptyLinks()
  for (const key of OVERVIEW_LINK_KEYS) {
    const state = record.links[key]
    if (!isLinkState(state)) return null
    links[key] = state
  }

  return {
    scope,
    updatedAt: record.updatedAt,
    batteryPercent: Number.isFinite(record.batteryPercent) ? record.batteryPercent! : null,
    batteryVoltage: Number.isFinite(record.batteryVoltage) ? record.batteryVoltage! : null,
    links,
  }
}

function readStoredSnapshot(scope: string) {
  if (!import.meta.client || !scope) return null
  try {
    return parseSnapshot(JSON.parse(sessionStorage.getItem(STORAGE_KEY) || 'null'), scope)
  } catch {
    return null
  }
}

function storeSnapshot(snapshot: OverviewRuntimeSnapshot) {
  if (!import.meta.client || !snapshot.scope) return
  try {
    sessionStorage.setItem(STORAGE_KEY, JSON.stringify(snapshot))
  } catch {
    /* 会话存储不可用时只保留当前 Vue 状态。 */
  }
}

function persistPendingSnapshot() {
  const next = pendingSnapshot
  pendingSnapshot = null
  if (!next) return

  storeSnapshot(next)
  lastPersistedAt = Date.now()
}

function queueSnapshot(snapshot: OverviewRuntimeSnapshot) {
  if (!import.meta.client || !snapshot.scope) return

  pendingSnapshot = snapshot
  if (persistTimer !== null) return

  const elapsed = Date.now() - lastPersistedAt
  const delay = lastPersistedAt === 0
    ? OVERVIEW_RUNTIME_PERSIST_INTERVAL_MS
    : Math.max(0, OVERVIEW_RUNTIME_PERSIST_INTERVAL_MS - elapsed)

  persistTimer = setTimeout(() => {
    persistTimer = null
    persistPendingSnapshot()
  }, delay)
}

function discardQueuedSnapshot() {
  if (persistTimer !== null) {
    clearTimeout(persistTimer)
    persistTimer = null
  }
  pendingSnapshot = null
}

export function createOverviewRuntimeScope(
  session: Pick<AuthSession, 'isGuest' | 'username' | 'email'> | null | undefined,
  apiBase: unknown = '',
) {
  if (!session) return ''
  const endpoint = String(apiBase ?? '').trim().replace(/\/+$/, '').toLowerCase()
  return [
    endpoint || 'default-endpoint',
    session.isGuest ? 'guest' : 'account',
    session.username.trim().toLowerCase(),
    session.email.trim().toLowerCase(),
  ].join('|')
}

export function useOverviewRuntime() {
  const snapshot = useState<OverviewRuntimeSnapshot>('overview:runtime', () => emptySnapshot())

  function hydrate(scope: string) {
    if (!scope) {
      snapshot.value = emptySnapshot()
      return snapshot.value
    }

    if (
      snapshot.value.scope === scope
      && snapshot.value.updatedAt > 0
      && Date.now() - snapshot.value.updatedAt <= OVERVIEW_RUNTIME_MAX_AGE_MS
    ) return snapshot.value

    snapshot.value = readStoredSnapshot(scope) ?? emptySnapshot(scope)
    return snapshot.value
  }

  function update(scope: string, next: OverviewRuntimeUpdate) {
    if (!scope) return
    const current = hydrate(scope)
    snapshot.value = {
      ...current,
      ...next,
      scope,
      updatedAt: Date.now(),
      links: { ...current.links, ...next.links },
    }
    queueSnapshot(snapshot.value)
  }

  function flush() {
    if (persistTimer !== null) {
      clearTimeout(persistTimer)
      persistTimer = null
    }
    persistPendingSnapshot()
  }

  function clear() {
    discardQueuedSnapshot()
    lastPersistedAt = 0
    snapshot.value = emptySnapshot()
    if (!import.meta.client) return
    try {
      sessionStorage.removeItem(STORAGE_KEY)
    } catch {
      /* 会话存储不可用时无需额外处理。 */
    }
  }

  return { snapshot, hydrate, update, flush, clear }
}
