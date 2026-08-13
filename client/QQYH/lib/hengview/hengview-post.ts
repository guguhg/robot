/**
 * /hengview 专属后处理管线
 *
 * HalfFloat 渲染目标 + Bloom / Vignette / 边缘色差 / 胶片颗粒 / 蓝噪声 Dither。
 * 独立于共享的 PostProcessingManager，避免影响其他页面。
 */
import * as THREE from 'three'
import {
  BlendFunction,
  BloomEffect,
  ChromaticAberrationEffect,
  EffectComposer,
  EffectPass,
  NoiseEffect,
  RenderPass,
  VignetteEffect,
} from 'postprocessing'
import { BlueNoiseDitherEffect } from '../BlueNoiseDitherEffect'
import { generateBlueNoiseTexture } from '../BlueNoiseGenerator'

export interface HengviewPostOptions {
  /** 紧凑档：仅保留 Bloom + Vignette + Dither */
  lean: boolean
  multisampling: number
}

export class HengviewPost {
  private composer: EffectComposer
  private bloom: BloomEffect
  private chromatic: ChromaticAberrationEffect | null = null
  private noise: NoiseEffect | null = null
  private dither: BlueNoiseDitherEffect
  private blueNoise: THREE.Texture
  private baseBloomIntensity = 0.34
  private baseBloomThreshold = 0.82
  private lastBoost = 0

  constructor(
    renderer: THREE.WebGLRenderer,
    scene: THREE.Scene,
    camera: THREE.Camera,
    options: HengviewPostOptions,
  ) {
    this.composer = new EffectComposer(renderer, {
      frameBufferType: THREE.HalfFloatType,
      multisampling: options.multisampling,
    })
    this.composer.addPass(new RenderPass(scene, camera))

    this.bloom = new BloomEffect({
      intensity: this.baseBloomIntensity,
      luminanceThreshold: 0.82,
      luminanceSmoothing: 0.22,
      mipmapBlur: true,
    })

    const vignette = new VignetteEffect({ offset: 0.3, darkness: 0.42 })

    this.blueNoise = generateBlueNoiseTexture(64)
    this.dither = new BlueNoiseDitherEffect(this.blueNoise, 1.0)

    const effects: Array<BloomEffect | VignetteEffect | ChromaticAberrationEffect | NoiseEffect | BlueNoiseDitherEffect> = [
      this.bloom,
      vignette,
    ]

    if (!options.lean) {
      this.chromatic = new ChromaticAberrationEffect({
        offset: new THREE.Vector2(0.0011, 0.0011),
        radialModulation: true,
        modulationOffset: 0.32,
      })
      this.noise = new NoiseEffect({ blendFunction: BlendFunction.OVERLAY })
      this.noise.blendMode.opacity.value = 0.03
      effects.push(this.chromatic, this.noise)
    }

    effects.push(this.dither)
    this.composer.addPass(new EffectPass(camera, ...effects))
  }

  render(deltaSeconds: number) {
    this.composer.render(deltaSeconds)
  }

  setSize(width: number, height: number) {
    this.composer.setSize(width, height)
  }

  /** 开机脉冲等瞬时效果：临时压低 bloom 阈值让高光炸开一瞬 */
  setBloomBoost(boost: number) {
    this.lastBoost = THREE.MathUtils.clamp(boost, 0, 1)
    this.bloom.intensity = this.baseBloomIntensity + this.lastBoost * 0.5
    this.bloom.luminanceMaterial.threshold = this.baseBloomThreshold - this.lastBoost * 0.34
  }

  /** 日夜主题：白厅大面积亮面需要更高的 bloom 阈值与更低强度 */
  applyTheme(mix: number) {
    this.baseBloomIntensity = THREE.MathUtils.lerp(0.34, 0.2, mix)
    this.baseBloomThreshold = THREE.MathUtils.lerp(0.82, 0.96, mix)
    this.setBloomBoost(this.lastBoost)
  }

  /** 画质治理：低档时关闭色差与颗粒 */
  setCinematicExtras(enabled: boolean) {
    if (this.chromatic) this.chromatic.blendMode.opacity.value = enabled ? 1 : 0
    if (this.noise) this.noise.blendMode.opacity.value = enabled ? 0.03 : 0
  }

  dispose() {
    this.composer.dispose()
    this.blueNoise.dispose()
  }
}
