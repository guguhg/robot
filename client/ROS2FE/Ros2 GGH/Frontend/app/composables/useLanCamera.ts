import type { CameraImageFrame } from '@/lib/cwebapi/cwebapi-client'
import type { LinkState } from '@/composables/useRobot'

export interface LanCameraOptions {
  url?: string
  topic?: string
  onFrame?: (frame: CameraImageFrame) => void
}

export interface RosbridgeImageMessage {
  op?: string
  topic?: string
  msg?: {
    width?: unknown
    height?: unknown
    step?: unknown
    encoding?: unknown
    data?: unknown
  }
}

// Must match the runtime default in nuxt.config.ts; legacy robot examples still show 9090.
const DEFAULT_URL = 'ws://192.168.1.30:9091'
const DEFAULT_TOPIC = '/aurora/rgb/image_raw'

function readPositiveInteger(value: unknown, name: string) {
  const result = Number(value)
  if (!Number.isSafeInteger(result) || result <= 0) throw new Error(`局域网图像 ${name} 无效。`)
  return result
}

function decodeBytes(value: unknown) {
  if (typeof value === 'string') {
    const binary = atob(value)
    const bytes = new Uint8Array(binary.length)
    for (let index = 0; index < binary.length; index += 1) bytes[index] = binary.charCodeAt(index)
    return bytes
  }

  if (Array.isArray(value)) {
    const bytes = new Uint8Array(value.length)
    for (let index = 0; index < value.length; index += 1) {
      const byte = Number(value[index])
      if (!Number.isInteger(byte) || byte < 0 || byte > 255) throw new Error('局域网图像数据无效。')
      bytes[index] = byte
    }
    return bytes
  }

  throw new Error('局域网图像数据不是 base64 或字节数组。')
}

/** 将通信脚本中的 rgb8/bgr8 ROS Image 消息规范化为前端 Canvas 使用的 BGR8。 */
export function decodeRosbridgeImage(message: RosbridgeImageMessage): CameraImageFrame {
  const payload = message.msg
  if (!payload) throw new Error('局域网图像消息缺少 msg。')

  const width = readPositiveInteger(payload.width, '宽度')
  const height = readPositiveInteger(payload.height, '高度')
  const step = readPositiveInteger(payload.step, '步长')
  const encoding = String(payload.encoding || 'rgb8').toLowerCase()
  if (encoding !== 'bgr8' && encoding !== 'rgb8') {
    throw new Error(`暂不支持 ${encoding || '未知'} 图像编码。`)
  }
  if (step < width * 3) throw new Error('局域网图像步长小于 BGR8 最小行宽。')

  const required = step * height
  const bytes = decodeBytes(payload.data)
  if (bytes.length < required) throw new Error('局域网图像数据长度不足。')
  const normalized = bytes.slice(0, required)

  if (encoding === 'rgb8') {
    for (let y = 0; y < height; y += 1) {
      const row = y * step
      for (let x = 0; x < width; x += 1) {
        const offset = row + x * 3
        const red = normalized[offset]!
        normalized[offset] = normalized[offset + 2]!
        normalized[offset + 2] = red
      }
    }
  }

  return { width, height, step, data: normalized }
}

export function useLanCamera(options: LanCameraOptions = {}) {
  const runtimeConfig = useRuntimeConfig()
  const url = options.url
    || String((runtimeConfig.public as { rosbridgeCameraUrl?: string }).rosbridgeCameraUrl || DEFAULT_URL)
  const topic = options.topic
    || String((runtimeConfig.public as { rosbridgeCameraTopic?: string }).rosbridgeCameraTopic || DEFAULT_TOPIC)

  const state = ref<LinkState>('off')
  const error = ref('')
  let socket: WebSocket | null = null
  let reconnectTimer: ReturnType<typeof setTimeout> | undefined
  let generation = 0
  let active = false

  function clearReconnect() {
    clearTimeout(reconnectTimer)
    reconnectTimer = undefined
  }

  function scheduleReconnect(currentGeneration: number) {
    if (!active || currentGeneration !== generation || reconnectTimer) return
    reconnectTimer = setTimeout(() => {
      reconnectTimer = undefined
      connect(currentGeneration)
    }, 1500)
  }

  function connect(currentGeneration: number) {
    if (!active || currentGeneration !== generation) return
    state.value = 'connecting'

    let next: WebSocket
    try {
      next = new WebSocket(url)
    } catch (cause) {
      state.value = 'off'
      error.value = cause instanceof Error ? cause.message : '无法创建局域网摄像头连接。'
      scheduleReconnect(currentGeneration)
      return
    }

    socket = next
    next.addEventListener('open', () => {
      if (!active || currentGeneration !== generation) {
        next.close()
        return
      }
      next.send(JSON.stringify({ op: 'subscribe', topic }))
      error.value = ''
    })
    next.addEventListener('message', (event) => {
      if (!active || currentGeneration !== generation || typeof event.data !== 'string') return

      let message: RosbridgeImageMessage
      try {
        message = JSON.parse(event.data) as RosbridgeImageMessage
      } catch {
        return
      }
      if (message.op !== 'publish' || (message.topic && message.topic !== topic)) return

      try {
        const frame = decodeRosbridgeImage(message)
        options.onFrame?.(frame)
        state.value = 'ok'
        error.value = ''
      } catch (cause) {
        state.value = 'off'
        error.value = cause instanceof Error ? cause.message : '局域网图像解码失败。'
      }
    })
    next.addEventListener('error', () => {
      if (!active || currentGeneration !== generation) return
      state.value = 'off'
      error.value = '无法访问机器人局域网摄像头。'
    })
    next.addEventListener('close', () => {
      if (socket === next) socket = null
      if (!active || currentGeneration !== generation) return
      state.value = 'connecting'
      scheduleReconnect(currentGeneration)
    })
  }

  function start() {
    active = true
    generation += 1
    clearReconnect()
    socket?.close()
    socket = null
    error.value = ''
    state.value = 'connecting'
    connect(generation)
  }

  function stop() {
    active = false
    generation += 1
    clearReconnect()
    socket?.close()
    socket = null
    state.value = 'off'
    error.value = ''
  }

  onUnmounted(stop)

  return { state, error, url, topic, start, stop }
}
