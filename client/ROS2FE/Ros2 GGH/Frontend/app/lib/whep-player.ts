import {
  CWebApiError,
  type CWebApiClient,
  type StreamUrlResult,
} from '@/lib/cwebapi/cwebapi-client'

export interface WhepReceiverOptions {
  apiBase: string
  srsBaseUrl?: string
  rtcConfiguration?: RTCConfiguration
  iceGatheringTimeoutMs?: number
  onStateChange?: (state: RTCPeerConnectionState) => void
  onResolvedUrl?: (url: string) => void
}

export interface WhepStartResult {
  info: StreamUrlResult
  resolvedWhepUrl: string
}

interface OwnedVideoElement extends HTMLVideoElement {
  __zeroWhepOwner?: symbol
}

class WhepReceiverSupersededError extends Error {
  constructor() {
    super('WHEP receiver attempt was superseded.')
    this.name = 'WhepReceiverSupersededError'
  }
}

function isLoopback(hostname: string) {
  return hostname === 'localhost' || hostname === '127.0.0.1' || hostname === '[::1]' || hostname === '::1'
}

/** 保留后端签发的路径和 Token，只修正浏览器无法访问的 SRS 回环地址。 */
export function resolveWhepUrl(signedUrl: string, apiBase: string, srsBaseUrl = '') {
  const resolved = new URL(signedUrl)
  const override = srsBaseUrl.trim()

  if (override) {
    const target = new URL(override)
    resolved.protocol = target.protocol
    resolved.hostname = target.hostname
    resolved.port = target.port
    return resolved.toString()
  }

  const backend = new URL(apiBase)
  if (isLoopback(resolved.hostname) && !isLoopback(backend.hostname)) {
    resolved.hostname = backend.hostname
  }
  return resolved.toString()
}

function waitForIceGathering(peer: RTCPeerConnection, timeoutMs: number) {
  if (peer.iceGatheringState === 'complete') return Promise.resolve()

  return new Promise<void>((resolve) => {
    let settled = false
    const finish = () => {
      if (settled) return
      settled = true
      clearTimeout(timer)
      peer.removeEventListener('icegatheringstatechange', check)
      resolve()
    }
    const check = () => {
      if (peer.iceGatheringState === 'complete') finish()
    }
    const timer = setTimeout(finish, timeoutMs)
    peer.addEventListener('icegatheringstatechange', check)
  })
}

export function createWhepReceiver(
  api: CWebApiClient,
  video: HTMLVideoElement,
  options: WhepReceiverOptions,
) {
  let peer: RTCPeerConnection | null = null
  let attempt = 0
  const videoOwner = Symbol('zero-whep-receiver')
  const ownedVideo = video as OwnedVideoElement

  function isCurrentAttempt(currentAttempt: number) {
    return currentAttempt === attempt
  }

  function ensureCurrentAttempt(currentAttempt: number) {
    if (!isCurrentAttempt(currentAttempt)) throw new WhepReceiverSupersededError()
  }

  function clearVideoIfOwned() {
    if (ownedVideo.__zeroWhepOwner !== videoOwner) return
    video.srcObject = null
    delete ownedVideo.__zeroWhepOwner
  }

  function clearPeer() {
    peer?.close()
    peer = null
    clearVideoIfOwned()
  }

  function stop() {
    attempt += 1
    clearPeer()
  }

  async function start(deviceId: string): Promise<WhepStartResult> {
    const currentAttempt = ++attempt
    clearPeer()
    const info = await api.getStreamUrl(deviceId)
    ensureCurrentAttempt(currentAttempt)
    const resolvedWhepUrl = resolveWhepUrl(info.whepUrl, options.apiBase, options.srsBaseUrl)
    options.onResolvedUrl?.(resolvedWhepUrl)
    ensureCurrentAttempt(currentAttempt)

    const next = new RTCPeerConnection(options.rtcConfiguration)
    peer = next
    next.addTransceiver('video', { direction: 'recvonly' })
    next.addTransceiver('audio', { direction: 'recvonly' })
    next.ontrack = (event) => {
      if (!isCurrentAttempt(currentAttempt) || peer !== next) return
      ownedVideo.__zeroWhepOwner = videoOwner
      video.srcObject = event.streams[0] || new MediaStream([event.track])
      void video.play().catch(() => {})
    }
    next.onconnectionstatechange = () => {
      if (!isCurrentAttempt(currentAttempt) || peer !== next) return
      options.onStateChange?.(next.connectionState)
    }

    try {
      const offer = await next.createOffer()
      ensureCurrentAttempt(currentAttempt)
      await next.setLocalDescription(offer)
      ensureCurrentAttempt(currentAttempt)
      await waitForIceGathering(next, options.iceGatheringTimeoutMs ?? 2000)
      ensureCurrentAttempt(currentAttempt)
      const response = await fetch(resolvedWhepUrl, {
        method: 'POST',
        headers: { 'Content-Type': 'application/sdp' },
        body: next.localDescription?.sdp || offer.sdp,
      })
      ensureCurrentAttempt(currentAttempt)
      if (!response.ok) {
        const details = (await response.text().catch(() => '')).trim()
        throw new CWebApiError(details || `SRS WHEP 返回 HTTP ${response.status}`, {
          status: response.status,
          response,
        })
      }
      const answer = await response.text()
      ensureCurrentAttempt(currentAttempt)
      await next.setRemoteDescription({ type: 'answer', sdp: answer })
      ensureCurrentAttempt(currentAttempt)
      return { info, resolvedWhepUrl }
    } catch (error) {
      if (peer === next) stop()
      else next.close()
      throw error
    }
  }

  return { start, stop }
}
