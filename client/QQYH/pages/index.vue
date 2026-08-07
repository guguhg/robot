<script setup lang="ts">
import { company, serviceAreas } from '~/data/site-content'

const heroFilms = [
  { src: '/media/company/hero-film-01.mp4', poster: '/media/company/hero-poster-01.webp', label: '创新中心' },
  { src: '/media/company/hero-film-02.mp4', poster: '/media/company/hero-poster-02.webp', label: '成型工位' },
  { src: '/media/company/hero-film-03.mp4', poster: '/media/company/hero-poster-03.webp', label: '设备单元' },
  { src: '/media/company/hero-film-04.mp4', poster: '/media/company/hero-poster-04.webp', label: '车间协同' },
  { src: '/media/company/hero-film-05.mp4', poster: '/media/company/hero-poster-05.webp', label: '钢筋放线' },
  { src: '/media/company/hero-film-06.mp4', poster: '/media/company/hero-poster-06.webp', label: '品牌标识' },
] as const

type FilmDirection = 'next' | 'previous'

const FILM_TRANSITION_DURATION = 980
const FILM_PREPARE_TIMEOUT = 1200
const activeFilm = ref(0)
const outgoingFilmIndex = ref<number | null>(null)
const pendingFilmIndex = ref<number | null>(null)
const heroVideos = ref<HTMLVideoElement[]>([])
const reduceMotion = ref(true)
const filmDirection = ref<FilmDirection>('next')
const filmTransitioning = ref(false)
const readyFilms = reactive(new Set<number>())
const failedFilms = reactive(new Set<number>())
let filmTransitionTimer: ReturnType<typeof setTimeout> | undefined
let filmPreparationCleanup: (() => void) | undefined
let filmPreparationId = 0
let motionQuery: MediaQueryList | undefined

const currentFilm = computed(() => heroFilms[activeFilm.value] ?? heroFilms[0])
const filmPreparing = computed(() => pendingFilmIndex.value !== null)
const mountedFilms = computed(() => {
  const indices: number[] = []
  const normalizeIndex = (index: number) => (index + heroFilms.length) % heroFilms.length
  const addFilm = (index: number, force = false) => {
    if (indices.includes(index) || (!force && failedFilms.has(index))) return
    indices.push(index)
  }

  if (!reduceMotion.value) {
    addFilm(normalizeIndex(activeFilm.value - 1))
    addFilm(normalizeIndex(activeFilm.value + 1))
  }
  if (pendingFilmIndex.value !== null) addFilm(pendingFilmIndex.value, true)
  if (outgoingFilmIndex.value !== null) addFilm(outgoingFilmIndex.value, true)
  addFilm(activeFilm.value, true)

  return indices.map((index) => ({
    index,
    film: heroFilms[index] ?? heroFilms[0],
  }))
})

const clearFilmTransitionTimer = () => {
  if (filmTransitionTimer === undefined) return
  clearTimeout(filmTransitionTimer)
  filmTransitionTimer = undefined
}

const clearFilmPreparation = () => {
  filmPreparationCleanup?.()
  filmPreparationCleanup = undefined
}

const cancelFilmPreparation = () => {
  filmPreparationId += 1
  clearFilmPreparation()
  pendingFilmIndex.value = null
}

const completeFilmTransition = () => {
  clearFilmTransitionTimer()
  const outgoingVideo = outgoingFilmIndex.value === null
    ? undefined
    : getHeroVideo(outgoingFilmIndex.value)
  outgoingVideo?.pause()
  if (outgoingVideo) outgoingVideo.currentTime = 0
  outgoingFilmIndex.value = null
  filmTransitioning.value = false
}

const getFilmElementClass = (index: number) => {
  if (index !== activeFilm.value && index !== outgoingFilmIndex.value) {
    return 'corp-home-hero__film-preload'
  }

  const classes = ['corp-home-hero__image']
  if (filmTransitioning.value && index === outgoingFilmIndex.value) {
    classes.push(`corp-home-hero__image--leaving-${filmDirection.value}`)
  }
  if (filmTransitioning.value && index === activeFilm.value) {
    classes.push(`corp-home-hero__image--entering-${filmDirection.value}`)
  }
  return classes
}

const getFilmState = (index: number) => {
  if (index === outgoingFilmIndex.value) return 'leaving'
  if (index === activeFilm.value) return filmTransitioning.value ? 'entering' : 'active'
  if (index === pendingFilmIndex.value) return 'preparing'
  return 'preload'
}

const getHeroVideo = (index: number) => heroVideos.value.find(
  (video) => Number(video.dataset.filmIndex) === index,
)

const playHeroVideo = async (index: number) => {
  await nextTick()
  if (reduceMotion.value) return
  await getHeroVideo(index)?.play().catch(() => undefined)
}

const markFilmReady = (index: number) => {
  readyFilms.add(index)
}

const handleMotionPreference = (event: MediaQueryListEvent) => {
  reduceMotion.value = event.matches
  completeFilmTransition()

  if (event.matches) {
    heroVideos.value.forEach((video) => video.pause())
  }
  else {
    void playHeroVideo(activeFilm.value)
  }
}

const startFilmTransition = async (index: number, direction: FilmDirection) => {
  const previousIndex = activeFilm.value
  pendingFilmIndex.value = null
  filmDirection.value = direction

  if (reduceMotion.value) {
    getHeroVideo(previousIndex)?.pause()
    activeFilm.value = index
    await nextTick()
    getHeroVideo(index)?.pause()
    return
  }

  outgoingFilmIndex.value = previousIndex
  filmTransitioning.value = true
  activeFilm.value = index
  filmTransitionTimer = setTimeout(
    completeFilmTransition,
    FILM_TRANSITION_DURATION + 80,
  )
  await playHeroVideo(index)
}

const selectFilm = async (index: number, direction?: FilmDirection) => {
  if (!heroFilms[index] || filmTransitioning.value) return

  if (index === activeFilm.value) {
    cancelFilmPreparation()
    return
  }
  if (index === pendingFilmIndex.value) return

  const requestedDirection = direction ?? (index > activeFilm.value ? 'next' : 'previous')
  if (!getHeroVideo(index)) readyFilms.delete(index)
  clearFilmPreparation()
  const requestId = ++filmPreparationId
  pendingFilmIndex.value = index
  await nextTick()

  if (requestId !== filmPreparationId) return
  const targetVideo = getHeroVideo(index)
  if (!targetVideo) {
    pendingFilmIndex.value = null
    return
  }

  const beginTransition = () => {
    if (requestId !== filmPreparationId || pendingFilmIndex.value !== index) return
    clearFilmPreparation()
    void startFilmTransition(index, requestedDirection)
  }

  if (targetVideo.readyState >= HTMLMediaElement.HAVE_CURRENT_DATA) {
    markFilmReady(index)
    beginTransition()
    return
  }

  const handleReady = () => {
    markFilmReady(index)
    beginTransition()
  }
  const prepareTimer = setTimeout(beginTransition, FILM_PREPARE_TIMEOUT)

  targetVideo.addEventListener('loadeddata', handleReady)
  targetVideo.addEventListener('canplay', handleReady)
  filmPreparationCleanup = () => {
    clearTimeout(prepareTimer)
    targetVideo.removeEventListener('loadeddata', handleReady)
    targetVideo.removeEventListener('canplay', handleReady)
  }

  targetVideo.preload = 'auto'
  if (targetVideo.networkState === HTMLMediaElement.NETWORK_EMPTY) targetVideo.load()
}

const playNextFilm = () => {
  if (reduceMotion.value || filmTransitioning.value || filmPreparing.value) return

  for (let offset = 1; offset <= heroFilms.length; offset += 1) {
    const nextIndex = (activeFilm.value + offset) % heroFilms.length
    if (!failedFilms.has(nextIndex)) {
      void selectFilm(nextIndex, 'next')
      return
    }
  }
}

const handleFilmAnimationEnd = (event: AnimationEvent, index: number) => {
  if (
    index === activeFilm.value
    && event.animationName === `corp-film-enter-${filmDirection.value}`
  ) {
    completeFilmTransition()
  }
}

const handleFilmEnded = (index: number) => {
  if (index === activeFilm.value && index !== outgoingFilmIndex.value) playNextFilm()
}

const handleFilmError = (failedIndex: number) => {
  failedFilms.add(failedIndex)
  readyFilms.delete(failedIndex)
  if (failedIndex === pendingFilmIndex.value) {
    cancelFilmPreparation()
    playNextFilm()
    return
  }
  if (failedIndex === activeFilm.value) {
    completeFilmTransition()
    playNextFilm()
  }
}

onMounted(() => {
  motionQuery = window.matchMedia('(prefers-reduced-motion: reduce)')
  reduceMotion.value = motionQuery.matches
  motionQuery.addEventListener('change', handleMotionPreference)

  if (!reduceMotion.value) void playHeroVideo(activeFilm.value)
})

onBeforeUnmount(() => {
  cancelFilmPreparation()
  clearFilmTransitionTimer()
  motionQuery?.removeEventListener('change', handleMotionPreference)
})

useSeoMeta({
  title: '桥桥友河智能装备｜工程装备与自动化系统集成',
  description: company.statement,
  ogTitle: '桥桥友河智能装备',
  ogDescription: company.statement,
  ogImage: '/media/company/hero-production-base.webp',
  ogType: 'website',
})

useHead({
  script: [
    {
      type: 'application/ld+json',
      innerHTML: JSON.stringify({
        '@context': 'https://schema.org',
        '@type': 'Organization',
        name: company.name,
        address: {
          '@type': 'PostalAddress',
          addressCountry: 'CN',
          addressRegion: '湖南省',
          addressLocality: '衡阳市衡山县',
          streetAddress: '开云镇兴园路智能产业制造园 A1-4 栋',
          postalCode: company.postalCode,
        },
      }),
    },
  ],
})
</script>

<template>
  <SiteShell>
    <section class="corp-home-hero">
      <div class="corp-home-hero__film-stage">
        <video
          v-for="frame in mountedFilms"
          :key="frame.film.src"
          ref="heroVideos"
          :class="getFilmElementClass(frame.index)"
          :data-film-index="frame.index"
          :data-film-state="getFilmState(frame.index)"
          :data-film-ready="readyFilms.has(frame.index) ? 'true' : 'false'"
          :src="frame.film.src"
          :poster="frame.film.poster"
          :autoplay="!reduceMotion && frame.index === activeFilm"
          muted
          playsinline
          preload="auto"
          :aria-label="frame.index === activeFilm ? `桥桥友河智能装备${frame.film.label}画面` : undefined"
          :aria-hidden="frame.index === activeFilm ? undefined : 'true'"
          tabindex="-1"
          @animationend="handleFilmAnimationEnd($event, frame.index)"
          @loadeddata="markFilmReady(frame.index)"
          @canplay="markFilmReady(frame.index)"
          @ended="handleFilmEnded(frame.index)"
          @error="handleFilmError(frame.index)"
        />
      </div>
      <div class="corp-home-hero__veil" />
      <div class="corp-home-hero__rails" aria-hidden="true"><i /><i /><i /></div>

      <div class="corp-container corp-home-hero__content">
        <div class="corp-home-hero__meta">
          <span>QQYH / HENGSHAN / HUNAN</span>
          <span>INTELLIGENT EQUIPMENT</span>
        </div>
        <div class="corp-home-hero__copy">
          <span class="corp-kicker corp-kicker--light">ENGINEERING × AUTOMATION</span>
          <h1>让工程制造<br>更接近下一现场。</h1>
          <p>{{ company.statement }}</p>
          <div class="corp-home-hero__actions">
            <NuxtLink class="corp-button corp-button--primary" to="/about">了解我们</NuxtLink>
          </div>
        </div>
        <div class="corp-home-hero__foot">
          <span>FILM {{ String(activeFilm + 1).padStart(2, '0') }} / {{ String(heroFilms.length).padStart(2, '0') }}</span>
          <div
            class="corp-home-hero__film-nav"
            aria-label="首屏视频选择"
            :aria-busy="filmPreparing ? 'true' : undefined"
          >
            <button
              v-for="(film, index) in heroFilms"
              :key="film.src"
              type="button"
              :class="{
                'is-active': index === activeFilm,
                'is-pending': index === pendingFilmIndex,
              }"
              :disabled="filmTransitioning"
              :aria-label="`播放第 ${index + 1} 段：${film.label}`"
              :aria-current="index === activeFilm ? 'true' : undefined"
              @click="selectFilm(index)"
            >
              <span>{{ String(index + 1).padStart(2, '0') }}</span>
            </button>
          </div>
          <span>{{ currentFilm.label }}</span>
        </div>
      </div>
    </section>

    <section class="corp-home-intro corp-section">
      <div class="corp-container corp-home-intro__grid">
        <div class="corp-section-index">01</div>
        <div class="corp-home-intro__title">
          <span class="corp-kicker">WHO WE ARE</span>
          <h2>从装备到系统，<br>服务真实工程现场。</h2>
        </div>
        <div class="corp-home-intro__text">
          <p>{{ company.description }}</p>
          <NuxtLink class="corp-arrow-link" to="/about">
            <span>关于桥桥友河</span>
            <i aria-hidden="true">↗</i>
          </NuxtLink>
        </div>
      </div>
    </section>

    <section class="corp-focus corp-section">
      <div class="corp-container corp-focus__grid">
        <div class="corp-focus__media">
          <figure>
            <img src="/media/company/factory-overview.webp" alt="桥桥友河智能装备生产车间" loading="lazy">
            <figcaption>MANUFACTURING BASE</figcaption>
          </figure>
          <figure>
            <img src="/media/company/intelligent-line.webp" alt="桥桥友河智能加工产线现场" loading="lazy" decoding="async">
            <figcaption>INTELLIGENT LINE</figcaption>
          </figure>
        </div>
        <div class="corp-focus__content">
          <span class="corp-kicker">WHAT WE DO</span>
          <h1>器以载事，用而后成。</h1>
          <div class="corp-focus__list">
            <article v-for="item in serviceAreas" :key="item.index">
              <span>{{ item.index }}</span>
              <div>
                <h3>{{ item.title }}</h3>
                <p>{{ item.text }}</p>
              </div>
            </article>
          </div>
          <NuxtLink class="corp-button corp-button--dark" to="/about">查看企业档案</NuxtLink>
        </div>
      </div>
    </section>

    <section class="corp-home-base">
      <img src="/media/company/company-building.webp" alt="桥桥友河企业生产基地建筑" loading="lazy">
      <div class="corp-home-base__veil" />
      <div class="corp-container corp-home-base__copy">
        <span class="corp-kicker corp-kicker--light">BASED IN HENGSHAN</span>
        <h2>立足衡山，<br>面向工程现场。</h2>
        <p>{{ company.locationSummary }}</p>
        <dl class="corp-home-base__facts">
          <div>
            <dt>宏观区位</dt>
            <dd>{{ company.location.region }} · {{ company.location.road }}</dd>
          </div>
          <div>
            <dt>园区方向</dt>
            <dd>{{ company.location.park }}。{{ company.location.industry }}</dd>
          </div>
          <div>
            <dt>区域环境</dt>
            <dd>{{ company.location.surroundings }}</dd>
          </div>
          <div>
            <dt>交通连接</dt>
            <dd>{{ company.location.access }}</dd>
          </div>
        </dl>
      </div>
    </section>

    <section class="corp-home-team">
      <div class="corp-container">
        <div class="corp-home-team__heading">
          <span class="corp-kicker">OUR TEAM</span>
          <h2> </h2>
          <p>全员合影 · 创新中心楼前</p>
        </div>
        <figure class="corp-home-team__media">
          <img
            src="/media/company/team-portrait.webp"
            alt="桥桥友河员工在公司办公楼前合照"
            loading="lazy"
            decoding="async"
          >
          <figcaption>
            <span>QQYH TEAM</span>
            <span>HENGSHAN · HUNAN</span>
          </figcaption>
        </figure>
      </div>
    </section>

    <section class="corp-home-horizon" aria-label="衡山区域环境">
      <img
        class="corp-home-horizon__image"
        src="/media/company/hengshan-sunset.webp"
        alt="夕阳下的衡山与智能制造产业园区域"
        loading="lazy"
        decoding="async"
      >
      <div class="corp-home-horizon__veil" aria-hidden="true"></div>
      <div class="corp-container corp-home-horizon__content">
        <span class="corp-kicker corp-kicker--light">HENGSHAN AT DUSK</span>
        <h2>山水之间<br>冉冉兴起的新坐标。</h2>
        <p>自衡山而出，研发、制造与现场，一脉相承。</p>
        <NuxtLink class="corp-arrow-link corp-arrow-link--light" to="/about">
          <span>查看企业档案</span>
          <i aria-hidden="true">↗</i>
        </NuxtLink>
      </div>
    </section>
  </SiteShell>
</template>
