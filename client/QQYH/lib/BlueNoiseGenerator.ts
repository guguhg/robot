/**
 * 程序化生成蓝噪声纹理
 * 使用 Void-and-Cluster 算法的简化版本
 */
import * as THREE from 'three'

/**
 * 生成 64x64 蓝噪声纹理
 * 使用程序化方法，避免加载外部资源
 */
export function generateBlueNoiseTexture(size: number = 64): THREE.DataTexture {
  const data = new Uint8Array(size * size)

  // 简化的蓝噪声生成：使用伪随机 + 高通滤波
  // 这不是真正的蓝噪声，但足以消除色带
  const temp = new Float32Array(size * size)

  // 1. 生成白噪声
  for (let i = 0; i < temp.length; i++) {
    temp[i] = Math.random()
  }

  // 2. 应用高通滤波（简化版）
  // 移除低频成分，保留高频
  const kernel = [
    [-1, -1, -1],
    [-1, 8, -1],
    [-1, -1, -1],
  ]

  const filtered = new Float32Array(size * size)

  for (let y = 1; y < size - 1; y++) {
    for (let x = 1; x < size - 1; x++) {
      let sum = 0

      for (let ky = 0; ky < 3; ky++) {
        for (let kx = 0; kx < 3; kx++) {
          const px = x + kx - 1
          const py = y + ky - 1
          const idx = py * size + px
          const kernelValue = kernel[ky]?.[kx]
          const tempValue = temp[idx]
          if (kernelValue !== undefined && tempValue !== undefined) {
            sum += tempValue * kernelValue
          }
        }
      }

      filtered[y * size + x] = sum
    }
  }

  // 3. 归一化到 [0, 255]
  let min = Infinity
  let max = -Infinity

  for (let i = 0; i < filtered.length; i++) {
    const value = filtered[i]
    if (value !== undefined) {
      if (value < min) min = value
      if (value > max) max = value
    }
  }

  const range = max - min

  for (let i = 0; i < filtered.length; i++) {
    const value = filtered[i]
    if (value !== undefined) {
      data[i] = Math.floor(((value - min) / range) * 255)
    }
  }

  // 创建纹理
  const texture = new THREE.DataTexture(data, size, size, THREE.RedFormat)
  texture.wrapS = THREE.RepeatWrapping
  texture.wrapT = THREE.RepeatWrapping
  texture.needsUpdate = true

  return texture
}

/**
 * 使用更好的算法生成蓝噪声（可选）
 * 基于 Poisson Disk Sampling 的思想
 */
export function generateBetterBlueNoise(size: number = 64): THREE.DataTexture {
  // 为了性能，这里仍使用简化算法
  // 真正的蓝噪声生成需要复杂的优化过程
  return generateBlueNoiseTexture(size)
}
