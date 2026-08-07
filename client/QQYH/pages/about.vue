<script setup lang="ts">
import { company } from '~/data/site-content'

type AboutSlide = { src: string, label: string, tag: string }

const slides: AboutSlide[] = [
  { src: '/media/company/office-tower-dusk.webp', label: '创新中心办公楼', tag: 'OFFICE' },
  { src: '/media/company/plant-facade.webp', label: '制造厂房', tag: 'PLANT' },
  { src: '/media/company/park-aerial.webp', label: '产业园全景', tag: 'AERIAL' },
  { src: '/media/company/office-signage.webp', label: '楼宇标识', tag: 'SIGNAGE' },
]

// 与 corporate.css 中 corp-about-progress 动画时长保持一致
const SLIDE_DURATION = 5500

const active = ref(0)
const paused = ref(false)
const reduceMotion = ref(true)
const cycle = ref(0)
let timer: ReturnType<typeof setInterval> | undefined
let motionQuery: MediaQueryList | undefined

const stopTimer = () => {
  if (timer === undefined) return
  clearInterval(timer)
  timer = undefined
}

const restartTimer = () => {
  stopTimer()
  if (!import.meta.client) return
  if (reduceMotion.value || paused.value || document.hidden) return
  timer = setInterval(() => {
    active.value = (active.value + 1) % slides.length
    cycle.value += 1
  }, SLIDE_DURATION)
}

const goTo = (index: number) => {
  const next = (index + slides.length) % slides.length
  if (next === active.value) return
  active.value = next
  cycle.value += 1
  restartTimer()
}

const nextSlide = () => goTo(active.value + 1)
const prevSlide = () => goTo(active.value - 1)

const setPaused = (state: boolean) => {
  if (paused.value === state) return
  paused.value = state
  restartTimer()
}

const handleKeydown = (event: KeyboardEvent) => {
  if (event.key === 'ArrowRight') {
    event.preventDefault()
    nextSlide()
  }
  else if (event.key === 'ArrowLeft') {
    event.preventDefault()
    prevSlide()
  }
}

const handleMotionPreference = (event: MediaQueryListEvent) => {
  reduceMotion.value = event.matches
  restartTimer()
}

const handleVisibility = () => restartTimer()

onMounted(() => {
  motionQuery = window.matchMedia('(prefers-reduced-motion: reduce)')
  reduceMotion.value = motionQuery.matches
  motionQuery.addEventListener('change', handleMotionPreference)
  document.addEventListener('visibilitychange', handleVisibility)
  slides.forEach((slide) => {
    const img = new Image()
    img.src = slide.src
  })
  restartTimer()
})

onBeforeUnmount(() => {
  stopTimer()
  motionQuery?.removeEventListener('change', handleMotionPreference)
  document.removeEventListener('visibilitychange', handleVisibility)
})

useSeoMeta({
  title: '关于我们｜桥桥友河智能装备',
  description: company.description,
  ogImage: '/media/company/office-tower-dusk.webp',
})
</script>

<template>
  <SiteShell>
    <section class="corp-about-profile">
      <div class="corp-container corp-about-profile__frame">
        <div class="corp-about-profile__meta" aria-hidden="true">
          <span>QQYH / HENGSHAN / HUNAN</span>
          <span>EST. 2025 · 421342</span>
        </div>

        <div class="corp-about-profile__grid">
          <i class="corp-about-profile__tick corp-about-profile__tick--tl" aria-hidden="true" />
          <i class="corp-about-profile__tick corp-about-profile__tick--tr" aria-hidden="true" />
          <i class="corp-about-profile__tick corp-about-profile__tick--bl" aria-hidden="true" />
          <i class="corp-about-profile__tick corp-about-profile__tick--br" aria-hidden="true" />

          <div
            class="corp-about-profile__media"
            :class="{ 'is-paused': paused }"
            role="group"
            aria-roledescription="轮播"
            aria-label="企业园区影像"
            tabindex="0"
            @mouseenter="setPaused(true)"
            @mouseleave="setPaused(false)"
            @focusin="setPaused(true)"
            @focusout="setPaused(false)"
            @keydown="handleKeydown"
            @touchstart.passive="setPaused(true)"
            @touchend.passive="setPaused(false)"
          >
            <img
              v-for="(slide, index) in slides"
              :key="slide.src"
              class="corp-about-profile__slide"
              :class="{ 'is-active': index === active, 'is-even': index % 2 === 0 }"
              :src="slide.src"
              :alt="slide.label"
              :loading="index === 0 ? undefined : 'lazy'"
              decoding="async"
            >
            <div class="corp-about-profile__media-veil" aria-hidden="true" />
            <span class="corp-about-profile__media-topline" aria-hidden="true">QQYH / HENGSHAN / HUNAN</span>

            <div class="corp-about-profile__caption" aria-live="polite">
              <span :key="`tag-${cycle}`" class="corp-about-profile__caption-tag">
                {{ String(active + 1).padStart(2, '0') }} / {{ String(slides.length).padStart(2, '0') }} · {{ slides[active]?.tag }}
              </span>
              <strong :key="`label-${cycle}`">{{ slides[active]?.label }}</strong>
            </div>

            <div class="corp-about-profile__arrows">
              <button type="button" aria-label="上一张" @click="prevSlide"><span aria-hidden="true">‹</span></button>
              <button type="button" aria-label="下一张" @click="nextSlide"><span aria-hidden="true">›</span></button>
            </div>

            <div class="corp-about-profile__progress">
              <button
                v-for="(slide, index) in slides"
                :key="slide.src"
                type="button"
                :class="{ 'is-active': index === active }"
                :aria-label="`第 ${index + 1} 张：${slide.label}`"
                :aria-current="index === active ? 'true' : undefined"
                @click="goTo(index)"
              >
                <i :key="index === active ? `fill-${cycle}` : 'idle'" aria-hidden="true" />
              </button>
            </div>
          </div>

          <div class="corp-about-profile__copy">
            <span class="corp-kicker">ABOUT QQYH</span>
            <h1>企业档案</h1>
            <p class="corp-about-profile__intro">{{ company.description }}</p>
            <dl class="corp-about-profile__facts">
              <div>
                <dt>企业名称</dt>
                <dd>{{ company.name }}</dd>
              </div>
              <div>
                <dt>到访地址</dt>
                <dd>
                  <strong>{{ company.buildingName }}</strong>
                  {{ company.address }}
                  <small>邮政编码 {{ company.postalCode }}</small>
                </dd>
              </div>
            </dl>
            <div id="contact" class="corp-about-profile__contact-label">
              <span class="corp-kicker">商务联系 CONTACT</span>
            </div>
            <dl class="corp-about-profile__facts corp-about-profile__facts--contact">
              <div>
                <dt>联系电话</dt>
                <dd>{{ company.contact.phone }}</dd>
              </div>
              <div>
                <dt>电子邮箱</dt>
                <dd>{{ company.contact.email }}</dd>
              </div>
              <div>
                <dt>商务时间</dt>
                <dd>{{ company.contact.businessHours }}</dd>
              </div>
            </dl>
          </div>
        </div>
      </div>
    </section>
  </SiteShell>
</template>
