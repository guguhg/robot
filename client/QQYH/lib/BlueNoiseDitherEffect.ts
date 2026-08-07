/**
 * 蓝噪声 Dither Effect
 * 用于消除暗场色带
 */
import * as THREE from 'three'
import { Effect, BlendFunction } from 'postprocessing'
import { Uniform } from 'three'

const fragmentShader = `
uniform sampler2D tDither;
uniform vec2 resolution;
uniform float strength;

void mainImage(const in vec4 inputColor, const in vec2 uv, out vec4 outputColor) {
  // 计算屏幕空间像素坐标
  vec2 pixelCoord = uv * resolution;

  // 平铺 64x64 蓝噪声纹理
  vec2 ditherUv = fract(pixelCoord / 64.0);

  // 采样蓝噪声
  float noise = texture2D(tDither, ditherUv).r;

  // 将噪声范围从 [0,1] 映射到 [-0.5, 0.5]
  noise = (noise - 0.5);

  // 应用 dither（强度 1/255，正好是 8-bit 色阶的一个单位）
  vec3 dithered = inputColor.rgb + (noise * strength / 255.0);

  outputColor = vec4(dithered, inputColor.a);
}
`

export class BlueNoiseDitherEffect extends Effect {
  constructor(ditherTexture: THREE.Texture, strength: number = 1.0) {
    super('BlueNoiseDitherEffect', fragmentShader, {
      blendFunction: BlendFunction.NORMAL,
      uniforms: new Map<string, Uniform<any>>([
        ['tDither', new Uniform(ditherTexture)],
        ['resolution', new Uniform(new THREE.Vector2(1, 1))],
        ['strength', new Uniform(strength)],
      ]),
    })
  }

  /**
   * 更新分辨率
   */
  override setSize(width: number, height: number) {
    this.uniforms.get('resolution')!.value.set(width, height)
  }

  /**
   * 更新强度
   */
  setStrength(strength: number) {
    this.uniforms.get('strength')!.value = strength
  }
}
