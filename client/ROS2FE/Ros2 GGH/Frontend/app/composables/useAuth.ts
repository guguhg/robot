import {
  CWebApiClient,
  type LoginResponse,
  type RegisterResponse,
} from '@/lib/cwebapi/cwebapi-client'

export type AuthSession = LoginResponse
export type LogoutReason = 'manual' | 'expired' | 'unauthorized'

// 局域网认证实例可能在后端依赖恢复时延迟响应，不能在其返回前取消登录。
const REQUEST_TIMEOUT_MS = 60_000
const EXPIRY_SKEW_MS = 10_000
const SESSION_STORAGE_KEY = 'zerorobot:auth-session:v1'

function cloneSession(next: LoginResponse): AuthSession {
  return {
    token: next.token,
    expiresAt: next.expiresAt,
    username: next.username,
    email: next.email,
    permissions: [...next.permissions],
    isGuest: next.isGuest,
  }
}

function getSessionStorage() {
  if (typeof window === 'undefined') return null
  try {
    return window.sessionStorage
  } catch {
    return null
  }
}

function parseStoredSession(raw: string | null): AuthSession | null {
  if (!raw) return null
  try {
    const value = JSON.parse(raw) as Partial<AuthSession>
    const expiresAt = typeof value.expiresAt === 'string' ? Date.parse(value.expiresAt) : NaN
    if (
      typeof value.token !== 'string'
      || !value.token
      || typeof value.username !== 'string'
      || typeof value.email !== 'string'
      || !Array.isArray(value.permissions)
      || typeof value.isGuest !== 'boolean'
      || !Number.isFinite(expiresAt)
      || expiresAt <= Date.now() + EXPIRY_SKEW_MS
    ) return null
    return {
      token: value.token,
      expiresAt: value.expiresAt!,
      username: value.username,
      email: value.email,
      permissions: value.permissions.filter((permission): permission is string => typeof permission === 'string'),
      isGuest: value.isGuest,
    }
  } catch {
    return null
  }
}

export function useAuth() {
  const config = useRuntimeConfig()
  const session = useState<AuthSession | null>('auth:session', () => null)
  const authNotice = useState<string>('auth:notice', () => '')
  const sessionRestored = useState<boolean>('auth:session-restored', () => false)

  const isAuthenticated = computed(() => session.value !== null)
  const isGuest = computed(() => session.value?.isGuest ?? false)
  const isAdmin = computed(() => session.value?.permissions.includes('system.admin') ?? false)
  const displayName = computed(() => session.value?.username ?? '')

  function makePublicClient() {
    return new CWebApiClient({ baseUrl: config.public.apiBase })
  }

  function requestOptions() {
    return { signal: AbortSignal.timeout(REQUEST_TIMEOUT_MS) }
  }

  function persistSession(next: AuthSession) {
    try {
      getSessionStorage()?.setItem(SESSION_STORAGE_KEY, JSON.stringify(next))
    } catch {
      /* 存储不可用时仍保留当前页面内会话。 */
    }
  }

  function clearPersistedSession() {
    try {
      getSessionStorage()?.removeItem(SESSION_STORAGE_KEY)
    } catch {
      /* 隐私模式或浏览器策略下无需阻断退出。 */
    }
  }

  function restoreSession() {
    if (sessionRestored.value) return
    sessionRestored.value = true
    if (session.value) return

    const storage = getSessionStorage()
    const restored = parseStoredSession(storage?.getItem(SESSION_STORAGE_KEY) ?? null)
    if (!restored) {
      clearPersistedSession()
      return
    }
    session.value = restored
  }

  function setSession(next: LoginResponse) {
    const normalized = cloneSession(next)
    session.value = normalized
    persistSession(normalized)
    authNotice.value = ''
  }

  async function login(identifier: string, password: string) {
    const next = await makePublicClient().login(identifier, password, requestOptions())
    setSession(next)
    return next
  }

  async function loginAsGuest() {
    const next = await makePublicClient().guestLogin(requestOptions())
    setSession(next)
    return next
  }

  function register(username: string, email: string, password: string): Promise<RegisterResponse> {
    return makePublicClient().register(username, email, password, requestOptions())
  }

  function verifyEmail(email: string, code: string) {
    return makePublicClient().verifyEmail(email, code, requestOptions())
  }

  function resendVerification(email: string) {
    return makePublicClient().resendVerification(email, requestOptions())
  }

  function forgotPassword(email: string) {
    return makePublicClient().forgotPassword(email, requestOptions())
  }

  function resetPassword(email: string, code: string, newPassword: string) {
    return makePublicClient().resetPassword(email, code, newPassword, requestOptions())
  }

  function logout(reason: LogoutReason = 'manual') {
    clearPersistedSession()
    session.value = null
    if (reason === 'expired') authNotice.value = '会话已过期，请重新登录。'
    if (reason === 'unauthorized') authNotice.value = '登录状态已失效，请重新登录。若同一账号刚在其他端登录，请使用最新会话。'
  }

  function clearNotice() {
    authNotice.value = ''
  }

  function hasPermission(permission: string) {
    return session.value?.permissions.includes(permission) ?? false
  }

  function createAuthenticatedClient() {
    const tokenAtCreation = session.value?.token || ''
    return new CWebApiClient({
      baseUrl: config.public.apiBase,
      tokenProvider: () => session.value?.token,
      onUnauthorized: () => {
        /* 旧请求不能用自己的 401 清空之后重新登录得到的新 Token。 */
        if (tokenAtCreation && session.value?.token === tokenAtCreation) logout('unauthorized')
      },
    })
  }

  onMounted(() => {
    restoreSession()
  })

  function startExpiryGuard() {
    let timer: ReturnType<typeof setTimeout> | undefined

    const stop = watch(
      session,
      (next) => {
        clearTimeout(timer)
        if (!next) return

        const expiresAt = Date.parse(next.expiresAt)
        if (!Number.isFinite(expiresAt)) return

        const delay = expiresAt - Date.now() - EXPIRY_SKEW_MS
        if (delay <= 0) {
          logout('expired')
          return
        }

        timer = setTimeout(() => logout('expired'), Math.min(delay, 2_147_000_000))
      },
      { immediate: true },
    )

    return () => {
      clearTimeout(timer)
      stop()
    }
  }

  return {
    session,
    authNotice,
    isAuthenticated,
    isGuest,
    isAdmin,
    displayName,
    login,
    loginAsGuest,
    register,
    verifyEmail,
    resendVerification,
    forgotPassword,
    resetPassword,
    logout,
    clearNotice,
    hasPermission,
    createAuthenticatedClient,
    startExpiryGuard,
  }
}
