<script setup lang="ts">
import {
  CWebApiError,
  type Assignee,
  type OperatorState,
} from '@/lib/cwebapi/cwebapi-client'
import type { LinkState } from '@/composables/useRobot'

type NoticeTone = 'neutral' | 'success' | 'error'

const props = defineProps<{
  apiState: LinkState
  operator: OperatorState
  isAdmin: boolean
  refresh: () => Promise<OperatorState>
  listUsers: () => Promise<Assignee[]>
  assign: (userId: string) => Promise<OperatorState>
  reclaim: () => Promise<OperatorState>
}>()

const users = ref<Assignee[]>([])
const loadingUsers = ref(false)
const actionBusy = ref(false)
const notice = ref('')
const noticeTone = ref<NoticeTone>('neutral')
const pendingAction = ref<string | null>(null)
let confirmationTimer: ReturnType<typeof setTimeout> | undefined

const currentOperatorLabel = computed(() => {
  if (!props.operator.hasOperator) return '无人占用'
  return props.operator.username || props.operator.userId || '未知操作者'
})
const operatorStateLabel = computed(() => {
  if (!props.operator.hasOperator) return 'OPEN'
  return props.operator.isSelf ? 'CURRENT' : 'HELD'
})
const canManage = computed(() => props.isAdmin && props.apiState === 'ok')
const sortedUsers = computed(() => [...users.value].sort((left, right) => (
  Number(right.isOperator) - Number(left.isOperator)
  || left.username.localeCompare(right.username, 'zh-CN')
)))

function setNotice(message: string, tone: NoticeTone = 'neutral') {
  notice.value = message
  noticeTone.value = tone
}

function clearPendingAction() {
  clearTimeout(confirmationTimer)
  confirmationTimer = undefined
  pendingAction.value = null
}

function armAction(action: string, message: string) {
  clearPendingAction()
  pendingAction.value = action
  setNotice(message)
  confirmationTimer = setTimeout(() => {
    if (pendingAction.value !== action) return
    pendingAction.value = null
    setNotice('确认已超时。')
  }, 4000)
}

function describeError(error: unknown) {
  if (!(error instanceof CWebApiError)) return error instanceof Error ? error.message : '操作者操作失败。'
  if (error.status === 401) return '登录状态已失效。'
  if (error.status === 403) return '当前账户没有管理员权限。'
  if (error.status === 404) return '目标账户不存在或已不可用。'
  return error.message || '操作者操作失败。'
}

function syncUsers(state: OperatorState) {
  users.value = users.value.map(user => ({
    ...user,
    isOperator: state.hasOperator && user.userId === state.userId,
  }))
}

async function refreshCurrent() {
  if (actionBusy.value) return
  try {
    const state = await props.refresh()
    syncUsers(state)
    setNotice('操作者状态已刷新。', 'success')
  } catch (error) {
    setNotice(describeError(error), 'error')
  }
}

async function loadUsers() {
  if (!canManage.value || loadingUsers.value || actionBusy.value) return
  loadingUsers.value = true
  try {
    users.value = await props.listUsers()
    syncUsers(props.operator)
    setNotice(`已读取 ${users.value.length} 个可分配账户。`, 'success')
  } catch (error) {
    setNotice(describeError(error), 'error')
  } finally {
    loadingUsers.value = false
  }
}

async function executeReclaim() {
  actionBusy.value = true
  clearPendingAction()
  try {
    const state = await props.reclaim()
    syncUsers(state)
    setNotice('已提交回收操作权。', 'success')
  } catch (error) {
    setNotice(describeError(error), 'error')
  } finally {
    actionBusy.value = false
  }
}

function requestReclaim() {
  if (!canManage.value || actionBusy.value) return
  if (props.operator.isSelf) {
    setNotice('当前账户已持有操作权。')
    return
  }
  if (pendingAction.value === 'reclaim') {
    void executeReclaim()
    return
  }
  armAction('reclaim', '再次点击“回收”确认切换操作权。')
}

async function executeAssign(user: Assignee) {
  actionBusy.value = true
  clearPendingAction()
  try {
    const state = await props.assign(user.userId)
    syncUsers(state)
    setNotice(`已提交移交给 ${user.username}。`, 'success')
  } catch (error) {
    setNotice(describeError(error), 'error')
  } finally {
    actionBusy.value = false
  }
}

function requestAssign(user: Assignee) {
  if (!canManage.value || actionBusy.value) return
  if (user.isOperator || user.userId === props.operator.userId) {
    setNotice(`${user.username} 已是当前操作者。`)
    return
  }
  const action = `assign:${user.userId}`
  if (pendingAction.value === action) {
    void executeAssign(user)
    return
  }
  armAction(action, `再次点击“移交”确认交给 ${user.username}。`)
}

watch(() => props.operator, syncUsers, { immediate: true })
watch(() => props.isAdmin, (isAdmin) => {
  if (isAdmin) return
  clearPendingAction()
  users.value = []
})

onUnmounted(clearPendingAction)
</script>

<template>
  <section class="operator-panel" aria-label="操作者管理">
    <header class="operator-head">
      <div class="media-toolbar-heading">
        <span class="media-status-dot" :class="`is-${apiState}`" aria-hidden="true"></span>
        <span class="media-toolbar-title hud-mono">OPERATOR</span>
        <span class="operator-scope hud-mono">{{ isAdmin ? 'ADMIN' : 'VIEW' }}</span>
      </div>
      <button
        type="button"
        title="刷新操作者状态"
        aria-label="刷新操作者状态"
        :disabled="actionBusy"
        @click="refreshCurrent"
      >
        <Icon name="lucide:refresh-cw" :class="{ 'media-spin': actionBusy }" size="15" />
      </button>
    </header>

    <section class="operator-current" aria-label="当前操作者">
      <span class="operator-current-label">当前操作者</span>
      <strong>{{ currentOperatorLabel }}</strong>
      <span class="operator-current-state hud-mono" :class="{ 'is-self': operator.isSelf }">
        {{ operatorStateLabel }}
      </span>
    </section>

    <section v-if="!isAdmin" class="operator-readonly" aria-label="操作者权限状态">
      <Icon name="lucide:lock-keyhole" size="14" />
      <span>{{ apiState === 'off' ? '等待登录态' : '当前账户仅可查看操作权状态' }}</span>
    </section>

    <template v-else>
      <section class="operator-actions" aria-label="操作权控制">
        <div class="operator-section-head">
          <span class="operator-section-label hud-mono">CONTROL</span>
          <button
            type="button"
            title="刷新可分配账户"
            aria-label="刷新可分配账户"
            :disabled="!canManage || loadingUsers || actionBusy"
            @click="loadUsers"
          >
            <Icon name="lucide:users-round" :class="{ 'media-spin': loadingUsers }" size="14" />
          </button>
        </div>
        <button
          type="button"
          class="operator-action"
          :class="{ 'is-confirming': pendingAction === 'reclaim' }"
          :disabled="!canManage || actionBusy"
          @click="requestReclaim"
        >
          <Icon name="lucide:rotate-ccw" size="14" />
          <span>{{ pendingAction === 'reclaim' ? '确认回收' : '回收操作权' }}</span>
        </button>
      </section>

      <p v-if="notice" class="operator-notice" :class="`is-${noticeTone}`" role="status" aria-live="polite">
        {{ notice }}
      </p>

      <section class="operator-users" aria-labelledby="operator-users-title">
        <div class="operator-section-head">
          <span id="operator-users-title" class="operator-section-label hud-mono">ASSIGNEES</span>
          <span class="operator-count hud-mono">{{ users.length || '--' }}</span>
        </div>
        <div v-if="users.length" class="operator-user-list">
          <div v-for="user in sortedUsers" :key="user.userId" class="operator-user-row">
            <div class="operator-user-name">
              <strong>{{ user.username }}</strong>
              <span class="hud-mono">{{ user.isOperator ? 'CURRENT' : 'READY' }}</span>
            </div>
            <button
              type="button"
              :class="{ 'is-confirming': pendingAction === `assign:${user.userId}` }"
              :disabled="!canManage || actionBusy || user.isOperator"
              :title="user.isOperator ? '当前操作者' : `移交操作权给 ${user.username}`"
              @click="requestAssign(user)"
            >
              <Icon :name="pendingAction === `assign:${user.userId}` ? 'lucide:check' : 'lucide:arrow-right-left'" size="13" />
              <span>{{ pendingAction === `assign:${user.userId}` ? '确认' : '移交' }}</span>
            </button>
          </div>
        </div>
        <button v-else type="button" class="operator-load-users" :disabled="!canManage || loadingUsers || actionBusy" @click="loadUsers">
          <Icon name="lucide:users-round" size="14" />
          <span>{{ loadingUsers ? '读取中' : '读取可分配账户' }}</span>
        </button>
      </section>
    </template>
  </section>
</template>

<style scoped>
.operator-panel {
  display: grid;
  gap: 12px;
  min-width: 0;
  color: var(--foreground);
}

.operator-head,
.operator-section-head,
.operator-current,
.operator-readonly,
.operator-user-row,
.operator-user-name {
  display: flex;
  align-items: center;
}

.operator-head {
  justify-content: space-between;
  min-height: 28px;
}

.operator-head > button,
.operator-section-head > button {
  display: inline-flex;
  align-items: center;
  justify-content: center;
  width: 28px;
  height: 28px;
  border: 0;
  border-radius: 4px;
  background: transparent;
  color: var(--muted-foreground);
  cursor: pointer;
}

.operator-head > button:hover,
.operator-head > button:focus-visible,
.operator-section-head > button:hover,
.operator-section-head > button:focus-visible {
  background: color-mix(in srgb, var(--foreground) 8%, transparent);
  color: var(--foreground);
  outline: none;
}

.operator-head > button:disabled,
.operator-section-head > button:disabled,
.operator-action:disabled,
.operator-user-row button:disabled,
.operator-load-users:disabled {
  cursor: not-allowed;
  opacity: 0.45;
}

.operator-scope,
.operator-section-label,
.operator-count {
  color: var(--ornament);
  font-size: 8px;
}

.operator-current {
  display: grid;
  grid-template-columns: minmax(0, 1fr) auto;
  gap: 3px 8px;
  min-height: 48px;
  padding: 9px 10px;
  border: 1px solid color-mix(in srgb, var(--border) 82%, transparent);
  background: color-mix(in srgb, var(--background) 44%, transparent);
}

.operator-current-label {
  color: var(--muted-foreground);
  font-size: 8.5px;
}

.operator-current strong {
  min-width: 0;
  color: var(--foreground);
  font-size: 12px;
  font-weight: 500;
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}

.operator-current-state {
  grid-row: span 2;
  align-self: center;
  color: #d97706;
  font-size: 8px;
}

.operator-current-state.is-self { color: #16a34a; }

.operator-readonly {
  min-height: 34px;
  gap: 7px;
  padding: 0 2px;
  color: var(--muted-foreground);
  font-size: 9px;
}

.operator-actions,
.operator-users {
  display: grid;
  gap: 7px;
  padding-top: 10px;
  border-top: 1px solid color-mix(in srgb, var(--border) 72%, transparent);
}

.operator-section-head { justify-content: space-between; }

.operator-section-head > button { width: 24px; height: 20px; }

.operator-action,
.operator-load-users {
  display: inline-flex;
  align-items: center;
  justify-content: center;
  gap: 7px;
  min-height: 30px;
  border: 1px solid var(--border);
  border-radius: 4px;
  background: transparent;
  color: var(--foreground);
  font-size: 9px;
  cursor: pointer;
  transition: border-color 0.16s ease, background-color 0.16s ease, color 0.16s ease, transform 0.16s ease;
}

.operator-action:hover:not(:disabled),
.operator-action:focus-visible,
.operator-load-users:hover:not(:disabled),
.operator-load-users:focus-visible {
  border-color: color-mix(in srgb, var(--primary) 60%, var(--border));
  background: color-mix(in srgb, var(--primary) 8%, transparent);
  color: var(--primary);
  outline: none;
  transform: translateY(-1px);
}

.operator-action.is-confirming,
.operator-user-row button.is-confirming {
  border-color: color-mix(in srgb, #d97706 68%, var(--border));
  background: color-mix(in srgb, #d97706 10%, transparent);
  color: #b45309;
}

.operator-notice {
  margin: -4px 0 0;
  color: var(--muted-foreground);
  font-size: 8.5px;
  line-height: 1.45;
}

.operator-notice.is-success { color: #15803d; }
.operator-notice.is-error { color: #dc2626; }

.operator-user-list {
  display: grid;
  border-top: 1px solid color-mix(in srgb, var(--border) 62%, transparent);
}

.operator-user-row {
  min-width: 0;
  min-height: 38px;
  gap: 8px;
  border-bottom: 1px solid color-mix(in srgb, var(--border) 62%, transparent);
}

.operator-user-name {
  min-width: 0;
  flex: 1;
  gap: 6px;
}

.operator-user-name strong {
  min-width: 0;
  color: var(--foreground);
  font-size: 9px;
  font-weight: 500;
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}

.operator-user-name span {
  flex: none;
  color: var(--ornament);
  font-size: 7px;
}

.operator-user-row button {
  display: inline-flex;
  align-items: center;
  justify-content: center;
  gap: 4px;
  min-width: 48px;
  height: 24px;
  border: 1px solid var(--border);
  border-radius: 3px;
  background: transparent;
  color: var(--muted-foreground);
  font-size: 8px;
  cursor: pointer;
  transition: border-color 0.16s ease, background-color 0.16s ease, color 0.16s ease;
}

.operator-user-row button:hover:not(:disabled),
.operator-user-row button:focus-visible {
  border-color: color-mix(in srgb, var(--primary) 60%, var(--border));
  color: var(--primary);
  outline: none;
}

.operator-load-users { width: 100%; }

@media (prefers-reduced-motion: reduce) {
  .operator-action,
  .operator-load-users { transition: none; }
  .operator-action:hover:not(:disabled),
  .operator-action:focus-visible,
  .operator-load-users:hover:not(:disabled),
  .operator-load-users:focus-visible { transform: none; }
}
</style>
