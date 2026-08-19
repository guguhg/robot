<script setup lang="ts">
import {
  DialogContent,
  DialogDescription,
  DialogOverlay,
  DialogPortal,
  DialogRoot,
  DialogTitle,
} from 'reka-ui'
import { CWebApiError } from '@/lib/cwebapi/cwebapi-client'

type Screen = 'login' | 'register' | 'verify' | 'forgot' | 'reset'

const open = defineModel<boolean>({ required: true })

const {
  authNotice,
  clearNotice,
  login,
  register,
  verifyEmail,
  resendVerification,
  forgotPassword,
  resetPassword,
} = useAuth()

const screen = ref<Screen>('login')
const busy = ref(false)
const errorMessage = ref('')
const statusMessage = ref('')
const verificationEmailSent = ref(true)

const loginForm = reactive({ identifier: '', password: '' })
const registerForm = reactive({ username: '', email: '', password: '', confirmPassword: '' })
const verifyForm = reactive({ email: '', code: '' })
const forgotForm = reactive({ email: '' })
const resetForm = reactive({ email: '', code: '', newPassword: '', confirmPassword: '' })

const showLoginPassword = ref(false)
const showRegisterPassword = ref(false)
const showResetPassword = ref(false)

const title = computed(() => {
  if (screen.value === 'register') return '创建账户'
  if (screen.value === 'verify') return '验证邮箱'
  if (screen.value === 'forgot') return '找回密码'
  if (screen.value === 'reset') return '重置密码'
  return '账户登录'
})

const USERNAME_PATTERN = /^[\p{L}\p{N}_]{2,32}$/u
const EMAIL_PATTERN = /^[^\s@]+@[^\s@]+\.[^\s@]+$/

function setScreen(next: Screen) {
  screen.value = next
  errorMessage.value = ''
  statusMessage.value = ''
}

function describeError(error: unknown) {
  if (!(error instanceof CWebApiError)) return '操作失败，请稍后重试。'

  const codeMessages: Record<string, string> = {
    email_not_verified: '邮箱尚未验证。',
    username_taken: '该用户名已被使用。',
    email_taken: '该邮箱已被注册。',
    invalid_username: '用户名格式不正确。',
    invalid_email: '邮箱格式不正确。',
    weak_password: '密码强度不足。',
    invalid_code: '验证码不正确。',
    code_expired: '验证码已过期，请重新获取。',
    too_many_attempts: '尝试次数过多，请稍后再试。',
    cooldown_active: '发送过于频繁，请稍后再试。',
    same_as_current: '新密码不能与当前密码相同。',
    rate_limited: '请求过于频繁，请稍后再试。',
  }

  let message = codeMessages[error.code]
  if (!message && error.status === 401) message = '用户名、邮箱或密码错误。'
  if (!message && error.status === 403) message = '当前账户无权执行此操作。'
  if (!message && error.status === 429) {
    const retryAfter = error.response?.headers.get('Retry-After')
    message = retryAfter ? `请求过于频繁，请在 ${retryAfter} 秒后重试。` : '请求过于频繁，请稍后再试。'
  }
  if (!message && error.status === 503) message = '认证服务暂不可用，请稍后重试。'
  if (!message && error.status === 0) message = '无法连接认证服务，请检查网络后重试。'
  if (!message) message = error.message || '操作失败，请稍后重试。'

  return error.correlationId ? `${message} 请求编号：${error.correlationId}` : message
}

async function run(action: () => Promise<void>) {
  if (busy.value) return
  busy.value = true
  errorMessage.value = ''
  try {
    await action()
  } catch (error) {
    errorMessage.value = describeError(error)
  } finally {
    busy.value = false
  }
}

function validEmail(email: string) {
  return EMAIL_PATTERN.test(email.trim())
}

async function submitLogin() {
  const identifier = loginForm.identifier.trim()
  if (!identifier || !loginForm.password) {
    errorMessage.value = '请输入用户名或邮箱及密码。'
    return
  }

  await run(async () => {
    try {
      await login(identifier, loginForm.password)
      open.value = false
    } catch (error) {
      if (error instanceof CWebApiError && error.code === 'email_not_verified' && validEmail(identifier)) {
        verifyForm.email = identifier
        screen.value = 'verify'
      }
      throw error
    }
  })
}

async function submitRegister() {
  const username = registerForm.username.trim()
  const email = registerForm.email.trim()

  if (!USERNAME_PATTERN.test(username)) {
    errorMessage.value = '用户名需为 2–32 位字母、数字、下划线或中文。'
    return
  }
  if (!validEmail(email)) {
    errorMessage.value = '请输入有效邮箱。'
    return
  }
  if (registerForm.password.length < 6) {
    errorMessage.value = '密码至少需要 6 位。'
    return
  }
  if (registerForm.password !== registerForm.confirmPassword) {
    errorMessage.value = '两次输入的密码不一致。'
    return
  }

  await run(async () => {
    const result = await register(username, email, registerForm.password)
    verifyForm.email = result.email
    forgotForm.email = result.email
    resetForm.email = result.email
    verificationEmailSent.value = result.verificationEmailSent
    screen.value = 'verify'
    statusMessage.value = result.verificationEmailSent
      ? '账号已创建，验证码已发送。'
      : '账号已创建，请重新发送验证码。'
  })
}

async function submitVerification() {
  const email = verifyForm.email.trim()
  const code = verifyForm.code.trim()
  if (!validEmail(email) || !code) {
    errorMessage.value = '请输入注册邮箱和验证码。'
    return
  }

  await run(async () => {
    await verifyEmail(email, code)
    loginForm.identifier = email
    loginForm.password = ''
    screen.value = 'login'
    statusMessage.value = '邮箱验证成功，请登录。'
  })
}

async function resendCode() {
  const email = verifyForm.email.trim()
  if (!validEmail(email)) {
    errorMessage.value = '请输入有效邮箱。'
    return
  }

  await run(async () => {
    await resendVerification(email)
    verificationEmailSent.value = true
    statusMessage.value = '若该邮箱对应待验证账户，验证码已重新发送。'
  })
}

async function submitForgotPassword() {
  const email = forgotForm.email.trim()
  if (!validEmail(email)) {
    errorMessage.value = '请输入有效邮箱。'
    return
  }

  await run(async () => {
    await forgotPassword(email)
    resetForm.email = email
    screen.value = 'reset'
    statusMessage.value = '若该邮箱已注册，重置验证码已发送。'
  })
}

async function submitResetPassword() {
  const email = resetForm.email.trim()
  const code = resetForm.code.trim()
  if (!validEmail(email) || !code) {
    errorMessage.value = '请输入邮箱和验证码。'
    return
  }
  if (resetForm.newPassword.length < 6) {
    errorMessage.value = '新密码至少需要 6 位。'
    return
  }
  if (resetForm.newPassword !== resetForm.confirmPassword) {
    errorMessage.value = '两次输入的新密码不一致。'
    return
  }

  await run(async () => {
    await resetPassword(email, code, resetForm.newPassword)
    loginForm.identifier = email
    loginForm.password = ''
    screen.value = 'login'
    statusMessage.value = '密码已重置，请重新登录。'
  })
}

watch(open, (isOpen) => {
  if (!isOpen) return
  screen.value = 'login'
  errorMessage.value = ''
  statusMessage.value = authNotice.value
  clearNotice()
})
</script>

<template>
  <DialogRoot v-model:open="open">
    <DialogPortal>
      <DialogOverlay class="auth-overlay" />
      <DialogContent class="auth-dialog">
        <div class="auth-heading">
          <button
            v-if="screen === 'verify' || screen === 'forgot' || screen === 'reset'"
            class="auth-icon-btn"
            type="button"
            title="返回登录"
            aria-label="返回登录"
            :disabled="busy"
            @click="setScreen('login')"
          >
            <Icon name="lucide:arrow-left" size="18" />
          </button>
          <span v-else class="auth-heading-spacer"></span>

          <div>
            <DialogTitle class="auth-title">{{ title }}</DialogTitle>
            <DialogDescription class="sr-only">Mio 账户认证</DialogDescription>
          </div>

          <button
            class="auth-icon-btn"
            type="button"
            title="关闭"
            aria-label="关闭"
            :disabled="busy"
            @click="open = false"
          >
            <Icon name="lucide:x" size="18" />
          </button>
        </div>

        <div v-if="screen === 'login' || screen === 'register'" class="auth-segment" role="tablist" aria-label="账户操作">
          <button
            type="button"
            role="tab"
            :aria-selected="screen === 'login'"
            :class="{ 'is-active': screen === 'login' }"
            @click="setScreen('login')"
          >
            登录
          </button>
          <button
            type="button"
            role="tab"
            :aria-selected="screen === 'register'"
            :class="{ 'is-active': screen === 'register' }"
            @click="setScreen('register')"
          >
            注册
          </button>
        </div>

        <p v-if="statusMessage" class="auth-message is-success" role="status">
          <Icon name="lucide:circle-check" size="15" />
          <span>{{ statusMessage }}</span>
        </p>
        <p v-if="errorMessage" class="auth-message is-error" role="alert">
          <Icon name="lucide:circle-alert" size="15" />
          <span>{{ errorMessage }}</span>
        </p>

        <form v-if="screen === 'login'" class="auth-form" @submit.prevent="submitLogin">
          <label class="auth-field">
            <span>用户名或邮箱</span>
            <span class="auth-input-wrap">
              <Icon name="lucide:user-round" size="16" />
              <input
                v-model="loginForm.identifier"
                name="username"
                type="text"
                autocomplete="username"
                spellcheck="false"
                :disabled="busy"
                required
              />
            </span>
          </label>

          <label class="auth-field">
            <span>密码</span>
            <span class="auth-input-wrap">
              <Icon name="lucide:lock-keyhole" size="16" />
              <input
                v-model="loginForm.password"
                name="password"
                :type="showLoginPassword ? 'text' : 'password'"
                autocomplete="current-password"
                :disabled="busy"
                required
              />
              <button
                type="button"
                class="auth-password-toggle"
                :title="showLoginPassword ? '隐藏密码' : '显示密码'"
                :aria-label="showLoginPassword ? '隐藏密码' : '显示密码'"
                @click="showLoginPassword = !showLoginPassword"
              >
                <Icon :name="showLoginPassword ? 'lucide:eye-off' : 'lucide:eye'" size="16" />
              </button>
            </span>
          </label>

          <button class="auth-link auth-forgot" type="button" :disabled="busy" @click="setScreen('forgot')">
            忘记密码
          </button>

          <button class="auth-submit" type="submit" :disabled="busy">
            <Icon v-if="busy" class="auth-spin" name="lucide:loader-circle" size="17" />
            <Icon v-else name="lucide:log-in" size="17" />
            登录
          </button>
        </form>

        <form v-else-if="screen === 'register'" class="auth-form" @submit.prevent="submitRegister">
          <label class="auth-field">
            <span>用户名</span>
            <span class="auth-input-wrap">
              <Icon name="lucide:user-round" size="16" />
              <input
                v-model="registerForm.username"
                name="new-username"
                type="text"
                autocomplete="username"
                minlength="2"
                maxlength="32"
                spellcheck="false"
                :disabled="busy"
                required
              />
            </span>
          </label>

          <label class="auth-field">
            <span>邮箱</span>
            <span class="auth-input-wrap">
              <Icon name="lucide:mail" size="16" />
              <input
                v-model="registerForm.email"
                name="email"
                type="email"
                autocomplete="email"
                :disabled="busy"
                required
              />
            </span>
          </label>

          <label class="auth-field">
            <span>密码</span>
            <span class="auth-input-wrap">
              <Icon name="lucide:lock-keyhole" size="16" />
              <input
                v-model="registerForm.password"
                name="new-password"
                :type="showRegisterPassword ? 'text' : 'password'"
                autocomplete="new-password"
                minlength="6"
                :disabled="busy"
                required
              />
              <button
                type="button"
                class="auth-password-toggle"
                :title="showRegisterPassword ? '隐藏密码' : '显示密码'"
                :aria-label="showRegisterPassword ? '隐藏密码' : '显示密码'"
                @click="showRegisterPassword = !showRegisterPassword"
              >
                <Icon :name="showRegisterPassword ? 'lucide:eye-off' : 'lucide:eye'" size="16" />
              </button>
            </span>
          </label>

          <label class="auth-field">
            <span>确认密码</span>
            <span class="auth-input-wrap">
              <Icon name="lucide:shield-check" size="16" />
              <input
                v-model="registerForm.confirmPassword"
                name="confirm-password"
                :type="showRegisterPassword ? 'text' : 'password'"
                autocomplete="new-password"
                minlength="6"
                :disabled="busy"
                required
              />
            </span>
          </label>

          <button class="auth-submit" type="submit" :disabled="busy">
            <Icon v-if="busy" class="auth-spin" name="lucide:loader-circle" size="17" />
            <Icon v-else name="lucide:user-plus" size="17" />
            创建账户
          </button>
        </form>

        <form v-else-if="screen === 'verify'" class="auth-form" @submit.prevent="submitVerification">
          <label class="auth-field">
            <span>注册邮箱</span>
            <span class="auth-input-wrap">
              <Icon name="lucide:mail" size="16" />
              <input v-model="verifyForm.email" type="email" autocomplete="email" :disabled="busy" required />
            </span>
          </label>
          <label class="auth-field">
            <span>邮箱验证码</span>
            <span class="auth-input-wrap">
              <Icon name="lucide:key-round" size="16" />
              <input
                v-model="verifyForm.code"
                type="text"
                inputmode="numeric"
                autocomplete="one-time-code"
                spellcheck="false"
                :disabled="busy"
                required
              />
            </span>
          </label>
          <button class="auth-submit" type="submit" :disabled="busy">
            <Icon v-if="busy" class="auth-spin" name="lucide:loader-circle" size="17" />
            <Icon v-else name="lucide:badge-check" size="17" />
            验证邮箱
          </button>
          <button class="auth-link auth-link-center" type="button" :disabled="busy" @click="resendCode">
            {{ verificationEmailSent ? '重新发送验证码' : '发送验证码' }}
          </button>
        </form>

        <form v-else-if="screen === 'forgot'" class="auth-form" @submit.prevent="submitForgotPassword">
          <label class="auth-field">
            <span>注册邮箱</span>
            <span class="auth-input-wrap">
              <Icon name="lucide:mail" size="16" />
              <input v-model="forgotForm.email" type="email" autocomplete="email" :disabled="busy" required />
            </span>
          </label>
          <button class="auth-submit" type="submit" :disabled="busy">
            <Icon v-if="busy" class="auth-spin" name="lucide:loader-circle" size="17" />
            <Icon v-else name="lucide:send" size="17" />
            发送验证码
          </button>
        </form>

        <form v-else class="auth-form" @submit.prevent="submitResetPassword">
          <label class="auth-field">
            <span>注册邮箱</span>
            <span class="auth-input-wrap">
              <Icon name="lucide:mail" size="16" />
              <input v-model="resetForm.email" type="email" autocomplete="email" :disabled="busy" required />
            </span>
          </label>
          <label class="auth-field">
            <span>重置验证码</span>
            <span class="auth-input-wrap">
              <Icon name="lucide:key-round" size="16" />
              <input
                v-model="resetForm.code"
                type="text"
                inputmode="numeric"
                autocomplete="one-time-code"
                spellcheck="false"
                :disabled="busy"
                required
              />
            </span>
          </label>
          <label class="auth-field">
            <span>新密码</span>
            <span class="auth-input-wrap">
              <Icon name="lucide:lock-keyhole" size="16" />
              <input
                v-model="resetForm.newPassword"
                :type="showResetPassword ? 'text' : 'password'"
                autocomplete="new-password"
                minlength="6"
                :disabled="busy"
                required
              />
              <button
                type="button"
                class="auth-password-toggle"
                :title="showResetPassword ? '隐藏密码' : '显示密码'"
                :aria-label="showResetPassword ? '隐藏密码' : '显示密码'"
                @click="showResetPassword = !showResetPassword"
              >
                <Icon :name="showResetPassword ? 'lucide:eye-off' : 'lucide:eye'" size="16" />
              </button>
            </span>
          </label>
          <label class="auth-field">
            <span>确认新密码</span>
            <span class="auth-input-wrap">
              <Icon name="lucide:shield-check" size="16" />
              <input
                v-model="resetForm.confirmPassword"
                :type="showResetPassword ? 'text' : 'password'"
                autocomplete="new-password"
                minlength="6"
                :disabled="busy"
                required
              />
            </span>
          </label>
          <button class="auth-submit" type="submit" :disabled="busy">
            <Icon v-if="busy" class="auth-spin" name="lucide:loader-circle" size="17" />
            <Icon v-else name="lucide:rotate-ccw-key" size="17" />
            重置密码
          </button>
        </form>
      </DialogContent>
    </DialogPortal>
  </DialogRoot>
</template>

<style>
.auth-overlay {
  position: fixed;
  inset: 0;
  z-index: 100;
  background: rgba(17, 24, 39, 0.34);
  backdrop-filter: blur(4px);
  animation: auth-fade-in 0.16s ease-out;
}

.auth-dialog {
  position: fixed;
  left: 50%;
  top: 50%;
  z-index: 101;
  width: min(calc(100vw - 32px), 420px);
  max-height: min(720px, calc(100dvh - 32px));
  overflow-y: auto;
  transform: translate(-50%, -50%);
  border: 1px solid var(--border);
  border-radius: 8px;
  background: var(--background);
  padding: 20px;
  box-shadow: 0 20px 60px rgba(17, 24, 39, 0.18);
  animation: auth-dialog-in 0.2s ease-out;
}

.auth-heading {
  display: grid;
  grid-template-columns: 34px 1fr 34px;
  align-items: center;
  gap: 8px;
  margin-bottom: 18px;
}

.auth-title {
  margin: 0;
  text-align: center;
  font-size: 17px;
  line-height: 1.35;
  font-weight: 600;
  letter-spacing: 0;
}

.auth-heading-spacer,
.auth-icon-btn {
  width: 34px;
  height: 34px;
}

.auth-icon-btn {
  display: inline-flex;
  align-items: center;
  justify-content: center;
  border: 0;
  border-radius: 6px;
  background: transparent;
  color: var(--muted-foreground);
  cursor: pointer;
}

.auth-icon-btn:hover {
  background: var(--muted);
  color: var(--foreground);
}

.auth-icon-btn:disabled {
  cursor: default;
  opacity: 0.45;
}

.auth-segment {
  display: grid;
  grid-template-columns: 1fr 1fr;
  height: 38px;
  margin-bottom: 18px;
  border: 1px solid var(--border);
  border-radius: 7px;
  background: var(--muted);
  padding: 3px;
}

.auth-segment button {
  border: 0;
  border-radius: 5px;
  background: transparent;
  color: var(--muted-foreground);
  cursor: pointer;
  font-size: 13px;
}

.auth-segment button.is-active {
  background: var(--background);
  color: var(--foreground);
  font-weight: 600;
  box-shadow: 0 1px 3px rgba(17, 24, 39, 0.08);
}

.auth-form {
  display: flex;
  flex-direction: column;
  gap: 14px;
}

.auth-field {
  display: flex;
  flex-direction: column;
  gap: 7px;
  color: var(--foreground);
  font-size: 12px;
  font-weight: 500;
}

.auth-input-wrap {
  position: relative;
  display: flex;
  align-items: center;
  height: 42px;
  border: 1px solid var(--input);
  border-radius: 7px;
  background: var(--background);
  color: var(--muted-foreground);
  transition: border-color 0.15s ease, box-shadow 0.15s ease;
}

.auth-input-wrap:focus-within {
  border-color: var(--ring);
  box-shadow: 0 0 0 3px color-mix(in srgb, var(--ring) 14%, transparent);
}

.auth-input-wrap > svg {
  margin-left: 12px;
  flex: none;
}

.auth-input-wrap input {
  min-width: 0;
  width: 100%;
  height: 100%;
  border: 0;
  outline: 0;
  background: transparent;
  padding: 0 12px;
  color: var(--foreground);
  font-size: 14px;
}

.auth-input-wrap input:disabled {
  cursor: wait;
  opacity: 0.65;
}

.auth-password-toggle {
  display: inline-flex;
  align-items: center;
  justify-content: center;
  width: 40px;
  height: 40px;
  flex: none;
  border: 0;
  background: transparent;
  color: var(--muted-foreground);
  cursor: pointer;
}

.auth-password-toggle:hover {
  color: var(--foreground);
}

.auth-submit {
  display: inline-flex;
  align-items: center;
  justify-content: center;
  gap: 8px;
  height: 42px;
  margin-top: 2px;
  border: 0;
  border-radius: 7px;
  background: var(--primary);
  color: var(--primary-foreground);
  font-size: 14px;
  font-weight: 600;
  cursor: pointer;
}

.auth-submit:hover:not(:disabled) {
  background: var(--primary-strong);
}

.auth-submit:disabled {
  cursor: wait;
  opacity: 0.7;
}

.auth-link {
  width: fit-content;
  border: 0;
  padding: 0;
  background: transparent;
  color: var(--primary);
  cursor: pointer;
  font-size: 12px;
}

.auth-link:hover:not(:disabled) {
  text-decoration: underline;
  text-underline-offset: 3px;
}

.auth-link:disabled {
  cursor: wait;
  opacity: 0.55;
}

.auth-forgot {
  align-self: flex-end;
  margin-top: -4px;
}

.auth-link-center {
  align-self: center;
}

.auth-message {
  display: flex;
  align-items: flex-start;
  gap: 8px;
  margin: 0 0 14px;
  border-radius: 6px;
  padding: 9px 10px;
  font-size: 12px;
  line-height: 1.5;
  overflow-wrap: anywhere;
}

.auth-message svg {
  margin-top: 1px;
  flex: none;
}

.auth-message.is-success {
  background: #f0fdf4;
  color: #166534;
}

.auth-message.is-error {
  background: #fef2f2;
  color: #991b1b;
}

.auth-spin {
  animation: auth-spin 0.8s linear infinite;
}

@keyframes auth-spin {
  to { transform: rotate(360deg); }
}

@keyframes auth-fade-in {
  from { opacity: 0; }
}

@keyframes auth-dialog-in {
  from {
    opacity: 0;
    transform: translate(-50%, calc(-50% + 8px)) scale(0.99);
  }
}

@media (max-width: 520px) {
  .auth-dialog {
    width: calc(100vw - 24px);
    max-height: calc(100dvh - 24px);
    padding: 18px 16px;
  }
}

@media (prefers-reduced-motion: reduce) {
  .auth-overlay,
  .auth-dialog,
  .auth-spin {
    animation: none;
  }
}
</style>
