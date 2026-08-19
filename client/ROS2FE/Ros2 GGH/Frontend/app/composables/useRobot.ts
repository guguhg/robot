import {
  CWebApiClient,
  type Assignee,
  type OperatorClient,
  type OperatorState,
  type RobotControlClient,
} from '@/lib/cwebapi/cwebapi-client'
import { useAuth, type AuthSession } from '@/composables/useAuth'

export type LinkState = 'ok' | 'connecting' | 'off'

export interface KeyboardControlState {
  w: boolean
  a: boolean
  s: boolean
  d: boolean
  shift: boolean
  space: boolean
}

/* 前端请求档位：常规 0.5 m/s，Shift / 摇杆满量程 1.0 m/s；最终安全边界仍由后端钳制。 */
const MAX_LIN = 1.0
const MAX_ANG = 1.0
const BASE_LINEAR_FACTOR = 0.5
const MIN_KEYBOARD_ANG = 0.1
const BASE_KEYBOARD_ANG = 0.5
const BASE_KEYBOARD_ANG_ACCELERATION = 0.8
const BOOST_KEYBOARD_ANG_ACCELERATION = 1.8
const KEYBOARD_ANG_DECELERATION = 2.0

const KEY_MAP: Record<string, 'w' | 'a' | 's' | 'd'> = {
  KeyW: 'w',
  KeyA: 'a',
  KeyS: 's',
  KeyD: 'd',
}

const EMPTY_OPERATOR_STATE: OperatorState = {
  hasOperator: false,
  userId: null,
  username: null,
  isSelf: false,
}

function resolveKeyboardAxes(keys: Pick<KeyboardControlState, 'w' | 'a' | 's' | 'd'>) {
  return {
    linear: (keys.w ? 1 : 0) - (keys.s ? 1 : 0),
    angular: (keys.a ? 1 : 0) - (keys.d ? 1 : 0),
  }
}

export function useRobot(options: { onEstop?: () => void } = {}) {
  const { session, logout, createAuthenticatedClient } = useAuth()

  const apiState = ref<LinkState>('off')
  const cmdState = ref<LinkState>('off')
  const operatorState = ref<OperatorState>({ ...EMPTY_OPERATOR_STATE })
  const isSelf = ref(false)
  const username = ref('')
  const commanded = ref(0)
  const controlError = ref('')
  const canControl = computed(() => isSelf.value && cmdState.value === 'ok')
  /* 指令矢量的视觉映像（旋钮坐标系，-1..1）：供摇杆旋钮与方向倾斜层使用 */
  const visual = ref({ x: 0, y: 0, m: 0 })

  const keys = reactive<KeyboardControlState>({
    w: false,
    a: false,
    s: false,
    d: false,
    shift: false,
    space: false,
  })
  const keyboard = readonly(keys)
  const estopSignal = ref(0)
  const joy = { x: 0, y: 0, force: 0 }

  let api: CWebApiClient | null = null
  let robot: RobotControlClient | null = null
  let operator: OperatorClient | null = null
  let removeOperatorHandler: (() => boolean) | null = null
  let stopSessionWatch: (() => void) | undefined
  let generation = 0
  let inputListenersActive = false
  let velocityLoopActive = false
  let keyboardTurnDirection = 0
  let keyboardTurnMagnitude = 0
  let keyboardTurnUpdatedAt = 0

  function resetKeyboardTurn() {
    keyboardTurnDirection = 0
    keyboardTurnMagnitude = 0
    keyboardTurnUpdatedAt = 0
  }

  function resolveKeyboardAngular(direction: number, boosted: boolean) {
    if (direction === 0) {
      resetKeyboardTurn()
      return 0
    }

    const now = Date.now()
    if (keyboardTurnDirection !== direction || keyboardTurnUpdatedAt === 0) {
      keyboardTurnDirection = direction
      keyboardTurnMagnitude = MIN_KEYBOARD_ANG
      keyboardTurnUpdatedAt = now
      return direction * keyboardTurnMagnitude
    }

    // 限制单次时间跨度，避免标签页恢复后把角速度跳到目标值。
    const elapsedSeconds = Math.min(0.2, Math.max(0, (now - keyboardTurnUpdatedAt) / 1000))
    keyboardTurnUpdatedAt = now

    const target = boosted ? MAX_ANG : BASE_KEYBOARD_ANG
    if (keyboardTurnMagnitude < target) {
      const acceleration = boosted
        ? BOOST_KEYBOARD_ANG_ACCELERATION
        : BASE_KEYBOARD_ANG_ACCELERATION
      keyboardTurnMagnitude = Math.min(target, keyboardTurnMagnitude + elapsedSeconds * acceleration)
    } else if (keyboardTurnMagnitude > target) {
      keyboardTurnMagnitude = Math.max(target, keyboardTurnMagnitude - elapsedSeconds * KEYBOARD_ANG_DECELERATION)
    }

    return direction * keyboardTurnMagnitude
  }

  function computeVelocity() {
    let lin = 0
    let ang = 0

    if (canControl.value) {
      if (joy.force > 0.05) {
        resetKeyboardTurn()
        lin = -joy.y * MAX_LIN
        ang = -joy.x * MAX_ANG
      } else {
        const linearFactor = keys.shift ? 1 : BASE_LINEAR_FACTOR
        const axes = resolveKeyboardAxes(keys)
        lin = axes.linear * MAX_LIN * linearFactor
        ang = resolveKeyboardAngular(axes.angular, keys.shift)
      }
    } else {
      resetKeyboardTurn()
    }

    commanded.value = Math.round(Math.abs(lin) * 100) / 100

    const vx = -(ang / MAX_ANG)
    const vy = -(lin / MAX_LIN)
    const magnitude = Math.max(Math.abs(vx), Math.abs(vy))
    const previous = visual.value
    if (
      Math.abs(previous.x - vx) > 0.001
      || Math.abs(previous.y - vy) > 0.001
      || Math.abs(previous.m - magnitude) > 0.001
    ) {
      visual.value = { x: vx, y: vy, m: magnitude }
    }

    return { linearX: lin, linearY: 0, angularZ: ang }
  }

  function hasDriveInput() {
    if (joy.force > 0.05) return true
    /* WA / WD / SA / SD 保持双轴叠加；同轴对按抵消后不能维持零速度循环。 */
    const axes = resolveKeyboardAxes(keys)
    return axes.linear !== 0 || axes.angular !== 0
  }

  function stopActiveVelocityLoop(sendStop = true) {
    const current = robot
    if (!velocityLoopActive || !current) {
      velocityLoopActive = false
      return
    }

    velocityLoopActive = false
    void current.stopVelocityLoop(sendStop).catch(error => {
      void handleRobotError(error, current, generation)
    })
  }

  function syncVelocityLoop() {
    computeVelocity()
    const current = robot
    if (!current || !canControl.value || !hasDriveInput()) {
      stopActiveVelocityLoop(true)
      return
    }
    if (velocityLoopActive) return

    velocityLoopActive = true
    current.beginVelocity(computeVelocity, 15)
  }

  function setJoystick(x: number, y: number, force: number) {
    if (!canControl.value) {
      releaseJoystick()
      return
    }
    joy.x = x
    joy.y = y
    joy.force = force
    syncVelocityLoop()
  }

  function releaseJoystick() {
    joy.x = 0
    joy.y = 0
    joy.force = 0
    syncVelocityLoop()
  }

  function clearDriveInputs() {
    keys.w = keys.a = keys.s = keys.d = keys.shift = false
    joy.x = 0
    joy.y = 0
    joy.force = 0
    computeVelocity()
  }

  function clearInputs() {
    clearDriveInputs()
    keys.space = false
  }

  async function estop(visualFeedback = true) {
    clearDriveInputs()
    stopActiveVelocityLoop(false)
    if (visualFeedback) {
      estopSignal.value += 1
      options.onEstop?.()
    }
    try {
      await robot?.stopRobot()
    } catch (error) {
      controlError.value = error instanceof Error ? error.message : '急停指令发送失败'
    }
  }

  function isTypingTarget(event: KeyboardEvent) {
    const target = event.target as HTMLElement | null
    return !!target && (target.tagName === 'INPUT' || target.tagName === 'TEXTAREA' || target.isContentEditable)
  }

  function onKeyDown(event: KeyboardEvent) {
    if (isTypingTarget(event) || !canControl.value) return
    if (event.code === 'Space') {
      event.preventDefault()
      keys.space = true
      if (!event.repeat) void estop()
      return
    }
    if (event.key === 'Shift') {
      keys.shift = true
      syncVelocityLoop()
      return
    }
    const key = KEY_MAP[event.code]
    if (key) {
      event.preventDefault()
      keys[key] = true
      syncVelocityLoop()
    }
  }

  function onKeyUp(event: KeyboardEvent) {
    if (event.code === 'Space') {
      keys.space = false
      return
    }
    if (event.key === 'Shift') {
      keys.shift = false
      syncVelocityLoop()
      return
    }
    const key = KEY_MAP[event.code]
    if (key) {
      keys[key] = false
      syncVelocityLoop()
    }
  }

  /* 只有手动驾驶进行中时，失焦 / 页面隐藏才需要向手动速度源停车。 */
  function onBlurLike() {
    clearInputs()
    stopActiveVelocityLoop(true)
  }

  function onVisibility() {
    if (document.hidden) onBlurLike()
  }

  function activateInputListeners() {
    if (inputListenersActive) return
    inputListenersActive = true
    window.addEventListener('keydown', onKeyDown)
    window.addEventListener('keyup', onKeyUp)
    window.addEventListener('blur', onBlurLike)
    document.addEventListener('visibilitychange', onVisibility)
  }

  function deactivateInputListeners() {
    if (!inputListenersActive) return
    inputListenersActive = false
    onBlurLike()
    window.removeEventListener('keydown', onKeyDown)
    window.removeEventListener('keyup', onKeyUp)
    window.removeEventListener('blur', onBlurLike)
    document.removeEventListener('visibilitychange', onVisibility)
  }

  async function stopRobotConnection() {
    const shouldStop = velocityLoopActive
    clearInputs()
    const current = robot
    robot = null
    velocityLoopActive = false
    cmdState.value = 'off'
    if (!current) return
    try {
      await current.stopVelocityLoop(shouldStop)
      await current.connection.stop()
    } catch {
      /* 断线或 Token 失效时无需重复报错 */
    }
  }

  async function teardownConnections() {
    removeOperatorHandler?.()
    removeOperatorHandler = null

    const currentOperator = operator
    operator = null
    await stopRobotConnection()

    if (currentOperator) {
      try {
        await currentOperator.stop()
      } catch {
        /* 已断开时忽略 */
      }
    }
  }

  async function handleRobotError(error: unknown, source: RobotControlClient, currentGeneration: number) {
    if (source !== robot || currentGeneration !== generation) return
    controlError.value = error instanceof Error ? error.message : '控制指令发送失败'
    await stopRobotConnection()

    try {
      const state = await api?.getCurrentOperator({ signal: AbortSignal.timeout(5000) })
      if (state && currentGeneration === generation) {
        operatorState.value = { ...state }
        isSelf.value = state.isSelf
      }
    } catch {
      /* REST 401 会由客户端回调清理会话 */
    }
  }

  async function startRobotConnection(currentGeneration: number) {
    if (!api || robot || !isSelf.value || currentGeneration !== generation) return
    cmdState.value = 'connecting'
    controlError.value = ''

    let next: RobotControlClient
    next = api.createRobotControl({
      onError: error => void handleRobotError(error, next, currentGeneration),
    })

    next.connection.onreconnecting(() => {
      if (next !== robot || currentGeneration !== generation) return
      clearInputs()
      velocityLoopActive = false
      cmdState.value = 'connecting'
      void next.stopVelocityLoop(false)
    })

    next.connection.onreconnected(async () => {
      if (next !== robot || currentGeneration !== generation || !api) return
      try {
        const state = await api.getCurrentOperator({ signal: AbortSignal.timeout(5000) })
        operatorState.value = { ...state }
        if (!state.isSelf) {
          isSelf.value = false
          await stopRobotConnection()
          return
        }
        isSelf.value = true
        cmdState.value = 'ok'
      } catch {
        await stopRobotConnection()
      }
    })

    next.connection.onclose(() => {
      if (next !== robot || currentGeneration !== generation) return
      clearInputs()
      robot = null
      velocityLoopActive = false
      cmdState.value = 'off'
    })

    try {
      await next.start()
      if (currentGeneration !== generation || !isSelf.value) {
        await next.stopVelocityLoop(false).catch(() => {})
        await next.connection.stop().catch(() => {})
        return
      }
      robot = next
      cmdState.value = 'ok'
    } catch (error) {
      cmdState.value = 'off'
      controlError.value = error instanceof Error ? error.message : 'CommandHub 连接失败'
      await next.stopVelocityLoop(false).catch(() => {})
      await next.connection.stop().catch(() => {})
    }
  }

  async function applyOperatorState(state: OperatorState, currentGeneration: number) {
    if (currentGeneration !== generation) return
    operatorState.value = { ...state }
    isSelf.value = state.isSelf
    if (!state.isSelf) {
      await stopRobotConnection()
      return
    }
    await startRobotConnection(currentGeneration)
  }

  async function connectForSession(nextSession: AuthSession | null) {
    const currentGeneration = ++generation
    await teardownConnections()
    if (currentGeneration !== generation) return

    api = null
    apiState.value = 'off'
    cmdState.value = 'off'
    operatorState.value = { ...EMPTY_OPERATOR_STATE }
    isSelf.value = false
    controlError.value = ''
    username.value = nextSession?.username ?? ''
    if (!nextSession) return

    apiState.value = 'connecting'
    const nextApi = createAuthenticatedClient()
    api = nextApi

    try {
      let nextOperatorState = await nextApi.getCurrentOperator({ signal: AbortSignal.timeout(5000) })
      if (currentGeneration !== generation) return
      apiState.value = 'ok'
      operatorState.value = { ...nextOperatorState }
      isSelf.value = nextOperatorState.isSelf

      /* 游客不能连接 ChatHub，也不能成为当前操作者。 */
      if (nextSession.isGuest) return

      const nextOperator = nextApi.createOperatorClient()
      const removeHandler = nextOperator.onChanged(state => void applyOperatorState(state, currentGeneration))
      try {
        await nextOperator.start()
      } catch (error) {
        removeHandler()
        controlError.value = error instanceof Error ? error.message : '操作者状态通道连接失败'
        return
      }

      if (currentGeneration !== generation) {
        removeHandler()
        await nextOperator.stop().catch(() => {})
        return
      }

      operator = nextOperator
      removeOperatorHandler = removeHandler
      nextOperatorState = await nextApi.getCurrentOperator({ signal: AbortSignal.timeout(5000) })
      await applyOperatorState(nextOperatorState, currentGeneration)
    } catch (error) {
      if (currentGeneration !== generation) return
      apiState.value = 'off'
      controlError.value = error instanceof Error ? error.message : '后端连接失败'
    }
  }

  async function refreshOperatorState() {
    const currentGeneration = generation
    if (!session.value) throw new Error('请先登录。')
    const state = await createAuthenticatedClient().getCurrentOperator({ signal: AbortSignal.timeout(8000) })
    await applyOperatorState(state, currentGeneration)
    return state
  }

  function listOperatorUsers(): Promise<Assignee[]> {
    if (!session.value) return Promise.reject(new Error('请先登录。'))
    return createAuthenticatedClient().getOperatorUsers({ signal: AbortSignal.timeout(8000) })
  }

  async function assignOperator(userId: string) {
    const currentGeneration = generation
    if (!session.value) throw new Error('请先登录。')
    const state = await createAuthenticatedClient().assignOperator(userId, { signal: AbortSignal.timeout(8000) })
    await applyOperatorState(state, currentGeneration)
    return state
  }

  async function reclaimOperator() {
    const currentGeneration = generation
    if (!session.value) throw new Error('请先登录。')
    const state = await createAuthenticatedClient().reclaimOperator({ signal: AbortSignal.timeout(8000) })
    await applyOperatorState(state, currentGeneration)
    return state
  }

  onMounted(() => {
    activateInputListeners()

    stopSessionWatch = watch(session, next => void connectForSession(next), { immediate: true })
  })

  onActivated(() => {
    activateInputListeners()
  })

  onDeactivated(() => {
    deactivateInputListeners()
  })

  onUnmounted(() => {
    generation += 1
    stopSessionWatch?.()
    deactivateInputListeners()
    void teardownConnections()
  })

  return {
    apiState,
    cmdState,
    operatorState,
    isSelf,
    canControl,
    username,
    commanded,
    controlError,
    speedLimit: MAX_LIN,
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
  }
}
