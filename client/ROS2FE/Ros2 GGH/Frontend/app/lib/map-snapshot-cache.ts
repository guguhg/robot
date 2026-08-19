import type { MapFrame } from '@/lib/cwebapi/cwebapi-client'

const DATABASE_NAME = 'zerorobot-map-snapshots'
const DATABASE_VERSION = 1
const STORE_NAME = 'maps'
const SNAPSHOT_SCHEMA_VERSION = 1
const MAX_MAP_CELLS = 4_194_304
const MAX_SNAPSHOT_AGE_MS = 7 * 24 * 60 * 60 * 1000

interface StoredMapSnapshot {
  schemaVersion: number
  scope: string
  savedAt: number
  width: number
  height: number
  resolution: number
  originX: number
  originY: number
  originYaw: number
  data: ArrayBuffer
}

export interface MapSnapshot {
  frame: MapFrame
  savedAt: number
}

export interface MapSnapshotScopeIdentity {
  isGuest: boolean
  username: string
  email: string
}

let databasePromise: Promise<IDBDatabase | null> | null = null

/* 同一后端地址下仍需按登录身份隔离；MapHub 当前没有设备 ID。 */
export function createMapSnapshotScope(apiBase: unknown, identity: MapSnapshotScopeIdentity) {
  const endpoint = String(apiBase ?? '').trim().replace(/\/+$/, '').toLowerCase()
  const account = identity.isGuest ? 'guest' : 'account'
  return [
    endpoint || 'default-endpoint',
    account,
    identity.username.trim().toLowerCase(),
    identity.email.trim().toLowerCase(),
  ].join('|')
}

function isPositiveInteger(value: unknown): value is number {
  return typeof value === 'number' && Number.isSafeInteger(value) && value > 0
}

function isFiniteNumber(value: unknown): value is number {
  return typeof value === 'number' && Number.isFinite(value)
}

function getCellCount(width: unknown, height: unknown) {
  if (!isPositiveInteger(width) || !isPositiveInteger(height)) return 0
  const count = width * height
  return Number.isSafeInteger(count) && count <= MAX_MAP_CELLS ? count : 0
}

function cloneData(data: Int8Array): ArrayBuffer {
  return new Uint8Array(data.buffer, data.byteOffset, data.byteLength).slice().buffer
}

function cloneArrayBuffer(value: unknown) {
  if (value instanceof ArrayBuffer) return value.slice(0)
  if (!ArrayBuffer.isView(value)) return null
  const view = value as ArrayBufferView
  return new Uint8Array(view.buffer, view.byteOffset, view.byteLength).slice().buffer
}

function toSnapshot(record: unknown, scope: string): MapSnapshot | null {
  if (!record || typeof record !== 'object') return null
  const value = record as Partial<StoredMapSnapshot>
  if (
    value.schemaVersion !== SNAPSHOT_SCHEMA_VERSION
    || value.scope !== scope
    || !isPositiveInteger(value.width)
    || !isPositiveInteger(value.height)
    || !isFiniteNumber(value.resolution)
    || value.resolution <= 0
    || !isFiniteNumber(value.originX)
    || !isFiniteNumber(value.originY)
    || !isFiniteNumber(value.originYaw)
    || !isFiniteNumber(value.savedAt)
    || value.savedAt <= 0
    || Date.now() - value.savedAt > MAX_SNAPSHOT_AGE_MS
  ) return null

  const cells = getCellCount(value.width, value.height)
  if (!cells) return null
  const buffer = cloneArrayBuffer(value.data)
  if (!buffer || buffer.byteLength < cells) return null
  return {
    savedAt: value.savedAt,
    frame: {
      width: value.width,
      height: value.height,
      resolution: value.resolution,
      originX: value.originX,
      originY: value.originY,
      originYaw: value.originYaw,
      data: new Int8Array(buffer.slice(0, cells)),
    },
  }
}

function openDatabase() {
  if (databasePromise) return databasePromise
  if (typeof indexedDB === 'undefined') return Promise.resolve<IDBDatabase | null>(null)

  databasePromise = new Promise<IDBDatabase | null>((resolve) => {
    const request = indexedDB.open(DATABASE_NAME, DATABASE_VERSION)
    request.onupgradeneeded = () => {
      const database = request.result
      if (!database.objectStoreNames.contains(STORE_NAME)) {
        database.createObjectStore(STORE_NAME, { keyPath: 'scope' })
      }
    }
    request.onsuccess = () => resolve(request.result)
    request.onerror = () => resolve(null)
    request.onblocked = () => resolve(null)
  })
  return databasePromise
}

function waitForTransaction(transaction: IDBTransaction) {
  return new Promise<void>((resolve, reject) => {
    transaction.oncomplete = () => resolve()
    transaction.onabort = () => reject(transaction.error ?? new Error('地图缓存事务已中止。'))
    transaction.onerror = () => reject(transaction.error ?? new Error('地图缓存事务失败。'))
  })
}

export async function loadMapSnapshot(scope: string): Promise<MapSnapshot | null> {
  if (!scope) return null
  const database = await openDatabase()
  if (!database) return null

  try {
    const transaction = database.transaction(STORE_NAME, 'readonly')
    const request = transaction.objectStore(STORE_NAME).get(scope)
    const record = await new Promise<unknown>((resolve, reject) => {
      request.onsuccess = () => resolve(request.result)
      request.onerror = () => reject(request.error)
    })
    return toSnapshot(record, scope)
  } catch {
    return null
  }
}

export async function saveMapSnapshot(scope: string, frame: MapFrame, savedAt = Date.now()) {
  const cells = getCellCount(frame.width, frame.height)
  if (
    !scope
    || !cells
    || frame.data.length < cells
    || !isFiniteNumber(frame.resolution)
    || frame.resolution <= 0
    || !isFiniteNumber(frame.originX)
    || !isFiniteNumber(frame.originY)
    || !isFiniteNumber(frame.originYaw)
    || !isFiniteNumber(savedAt)
  ) return false

  const database = await openDatabase()
  if (!database) return false

  const snapshot: StoredMapSnapshot = {
    schemaVersion: SNAPSHOT_SCHEMA_VERSION,
    scope,
    savedAt,
    width: frame.width,
    height: frame.height,
    resolution: frame.resolution,
    originX: frame.originX,
    originY: frame.originY,
    originYaw: frame.originYaw,
    data: cloneData(frame.data),
  }

  try {
    const transaction = database.transaction(STORE_NAME, 'readwrite')
    transaction.objectStore(STORE_NAME).put(snapshot)
    await waitForTransaction(transaction)
    return true
  } catch {
    return false
  }
}

export async function clearMapSnapshot(scope: string) {
  if (!scope) return false
  const database = await openDatabase()
  if (!database) return false

  try {
    const transaction = database.transaction(STORE_NAME, 'readwrite')
    transaction.objectStore(STORE_NAME).delete(scope)
    await waitForTransaction(transaction)
    return true
  } catch {
    return false
  }
}
