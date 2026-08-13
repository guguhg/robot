/**
 * /hengview 材质体系
 *
 * 1. 按 explode/hero GLB 的 14 套真实材质名做三档分级重调（碳黑结构 / 钢蓝壳体 / 机加合金），
 *    信号色压暗、镜头玻璃升级为 clearcoat 物理材质。
 * 2. 向每套材质注入「全息物化」shader：uScanY 以下渲染真实 PBR，以上渲染冰青全息鬼影，
 *    交界处有一条发光扫描缘。全程不透明，无透明排序问题。
 */
import * as THREE from 'three'

export interface MaterialGrade {
  color: number
  metalness: number
  roughness: number
  emissive?: number
  emissiveIntensity?: number
  clearcoat?: boolean
}

/** 按 GLB 材质名精确分级（explode 与 hero 资产共享同一材质表） */
const MATERIAL_GRADES: Record<string, MaterialGrade> = {
  // 碳黑结构层（约 73% 三角面）—— 近黑蓝灰，保留体积感，不做纯黑
  'VRayMtl_23': { color: 0x1C242E, metalness: 0.24, roughness: 0.68 },
  'VRayMtl_24': { color: 0x11161C, metalness: 0.1, roughness: 0.84 },
  'Plastic_Black_mtl #0': { color: 0x121619, metalness: 0.0, roughness: 0.58 },

  // 钢蓝壳体层（约 14%）—— 源模型饱和蓝压为深钢蓝，唯一大面积色彩
  'Material #25': { color: 0x24384C, metalness: 0.62, roughness: 0.46 },
  'xxxx': { color: 0x1B2836, metalness: 0.72, roughness: 0.42 },

  // 机加合金层（约 12%）—— 亮金属骨架与紧固件
  'MultiMat_11': { color: 0x7E8992, metalness: 0.88, roughness: 0.36 },
  'Material #51': { color: 0x99A3AC, metalness: 0.9, roughness: 0.3 },
  'Material #46': { color: 0x39424C, metalness: 0.84, roughness: 0.44 },
  'Material #30': { color: 0x6E7880, metalness: 0.88, roughness: 0.36 },
  'Material #30.001': { color: 0x828C94, metalness: 0.88, roughness: 0.34 },
  'Material #19': { color: 0x747E86, metalness: 0.86, roughness: 0.4 },

  // 信号色 —— 保留工程语义但脱饱和，避免 CAD 玩具感
  'Material #47': { color: 0x8C3226, metalness: 0.32, roughness: 0.58 },
  'Material #48': { color: 0x2C3B33, metalness: 0.36, roughness: 0.6 },

  // 镜头玻璃 —— 全模型唯一 clearcoat 焦点 + 冰青微发光「眼睛」
  'Glass_mtl #0': {
    color: 0x0D161F,
    metalness: 0.0,
    roughness: 0.12,
    emissive: 0x2FB9CC,
    emissiveIntensity: 0.0,
    clearcoat: true,
  },
}

const DEFAULT_GRADE: MaterialGrade = { color: 0x151B23, metalness: 0.2, roughness: 0.76 }

/** 全息物化共享 uniforms —— 所有被注入的材质引用同一份对象 */
export interface HoloUniforms {
  uScanY: { value: number }
  uHoloIntensity: { value: number }
  uHoloTime: { value: number }
  uEdgeWidth: { value: number }
  uHoloColor: { value: THREE.Color }
  /** 断层透察：切片带下缘 / 上缘 / 透察强度（带外转为 X-ray 全息） */
  uSliceLow: { value: number }
  uSliceHigh: { value: number }
  uSliceGhost: { value: number }
}

export const createHoloUniforms = (): HoloUniforms => ({
  uScanY: { value: -1000 },
  uHoloIntensity: { value: 0 },
  uHoloTime: { value: 0 },
  uEdgeWidth: { value: 0.05 },
  uHoloColor: { value: new THREE.Color(0x46D7EA) },
  uSliceLow: { value: -1000 },
  uSliceHigh: { value: -1000 },
  uSliceGhost: { value: 0 },
})

const HOLO_VERTEX_PATCH = /* glsl */ `
#include <project_vertex>
vHengviewWorldPos = (modelMatrix * vec4(transformed, 1.0)).xyz;
`

const HOLO_FRAGMENT_PATCH = /* glsl */ `
#include <dithering_fragment>
{
  // 物化遮罩：扫描面以下为 1（真实材质），以上为 0（全息/黑剪影）
    float materialized = smoothstep(vHengviewWorldPos.y, vHengviewWorldPos.y + uEdgeWidth, uScanY);

  vec3 viewDir = normalize(vViewPosition);
  vec3 holoNormal = normalize(vNormal);
  float fresnel = pow(1.0 - clamp(dot(holoNormal, viewDir), 0.0, 1.0), 2.6);
    float scanline = 0.5 + 0.5 * sin(vHengviewWorldPos.y * 240.0 - uHoloTime * 2.4);
    float flicker = 0.88 + 0.12 * sin(vHengviewWorldPos.x * 37.0 + uHoloTime * 21.0);
  vec3 holo = uHoloColor * (fresnel * 0.9 + scanline * 0.06 + 0.02) * flicker * uHoloIntensity;

  // 未物化区域：全息强度为 0 时是吞光黑剪影（开机前的"暗中存在"）
  gl_FragColor.rgb = mix(holo, gl_FragColor.rgb, materialized);

  // 扫描缘：贴近扫描面的一条发光热线
    float edge = 1.0 - smoothstep(0.0, uEdgeWidth * 2.4, abs(vHengviewWorldPos.y - uScanY));
  gl_FragColor.rgb += uHoloColor * edge * uHoloIntensity * 1.5;

  // 断层透察：切片带内保持实体，带外整体转为 X-ray 全息，上下缘各一条扫描热线
  if (uSliceGhost > 0.001) {
    float inBand = smoothstep(uSliceLow - uEdgeWidth, uSliceLow, vHengviewWorldPos.y)
      * (1.0 - smoothstep(uSliceHigh, uSliceHigh + uEdgeWidth, vHengviewWorldPos.y));
    float ghost = uSliceGhost * (1.0 - inBand);
    vec3 xray = uHoloColor * (fresnel * 0.72 + scanline * 0.05 + 0.028) * flicker;
    gl_FragColor.rgb = mix(gl_FragColor.rgb, xray, ghost);
    float sliceEdge = max(
      1.0 - smoothstep(0.0, uEdgeWidth * 1.8, abs(vHengviewWorldPos.y - uSliceLow)),
      1.0 - smoothstep(0.0, uEdgeWidth * 1.8, abs(vHengviewWorldPos.y - uSliceHigh))
    );
    gl_FragColor.rgb += uHoloColor * sliceEdge * uSliceGhost * 1.1;
  }
}
`

const injectHolo = (material: THREE.Material, uniforms: HoloUniforms) => {
  material.onBeforeCompile = (shader) => {
    shader.uniforms.uScanY = uniforms.uScanY
    shader.uniforms.uHoloIntensity = uniforms.uHoloIntensity
    shader.uniforms.uHoloTime = uniforms.uHoloTime
    shader.uniforms.uEdgeWidth = uniforms.uEdgeWidth
    shader.uniforms.uHoloColor = uniforms.uHoloColor
    shader.uniforms.uSliceLow = uniforms.uSliceLow
    shader.uniforms.uSliceHigh = uniforms.uSliceHigh
    shader.uniforms.uSliceGhost = uniforms.uSliceGhost

    shader.vertexShader = 'varying vec3 vHengviewWorldPos;\n' + shader.vertexShader
      .replace('#include <project_vertex>', HOLO_VERTEX_PATCH)

    shader.fragmentShader = [
      'varying vec3 vHengviewWorldPos;',
      'uniform float uScanY;',
      'uniform float uHoloIntensity;',
      'uniform float uHoloTime;',
      'uniform float uEdgeWidth;',
      'uniform vec3 uHoloColor;',
      'uniform float uSliceLow;',
      'uniform float uSliceHigh;',
      'uniform float uSliceGhost;',
    ].join('\n') + '\n' + shader.fragmentShader
      .replace('#include <dithering_fragment>', HOLO_FRAGMENT_PATCH)
  }
  material.customProgramCacheKey = () => 'hengview-holo-v1'
}

export interface RegradeResult {
  holoUniforms: HoloUniforms
  /** 镜头玻璃材质（「眼睛」发光由场景驱动） */
  eyeMaterials: THREE.MeshPhysicalMaterial[]
  materials: THREE.Material[]
}

/**
 * 对模型做材质分级 + 全息注入。
 * 同名材质在 GLB 内共享实例，因此每套只处理一次。
 */
export const regradeModelMaterials = (root: THREE.Object3D): RegradeResult => {
  const holoUniforms = createHoloUniforms()
  const eyeMaterials: THREE.MeshPhysicalMaterial[] = []
  const processed = new Map<string, THREE.Material>()
  const tracked: THREE.Material[] = []

  root.traverse((item) => {
    const mesh = item as THREE.Mesh
    if (!mesh.isMesh) return

    const materials = Array.isArray(mesh.material) ? mesh.material : [mesh.material]
    const replacements = materials.map((source) => {
      if (!source) return source

      const cached = processed.get(source.uuid)
      if (cached) return cached

      const grade = MATERIAL_GRADES[source.name] ?? DEFAULT_GRADE
      let material: THREE.Material

      if (grade.clearcoat) {
        const physical = new THREE.MeshPhysicalMaterial({
          name: source.name,
          color: grade.color,
          metalness: grade.metalness,
          roughness: grade.roughness,
          clearcoat: 1.0,
          clearcoatRoughness: 0.08,
          emissive: grade.emissive ?? 0x000000,
          emissiveIntensity: grade.emissiveIntensity ?? 0,
          envMapIntensity: 1.1,
        })
        eyeMaterials.push(physical)
        material = physical
      } else {
        const standard = source as THREE.MeshStandardMaterial
        if (standard.isMeshStandardMaterial) {
          standard.color.setHex(grade.color)
          standard.metalness = grade.metalness
          standard.roughness = grade.roughness
          standard.emissive.setHex(grade.emissive ?? 0x000000)
          standard.emissiveIntensity = grade.emissiveIntensity ?? 0
          standard.envMapIntensity = 0.85
          standard.map = null
          material = standard
        } else {
          material = source
        }
      }

      injectHolo(material, holoUniforms)
      material.needsUpdate = true
      processed.set(source.uuid, material)
      tracked.push(material)
      return material
    })

    mesh.material = Array.isArray(mesh.material)
      ? (replacements as THREE.Material[])
      : (replacements[0] as THREE.Material)
  })

  return { holoUniforms, eyeMaterials, materials: tracked }
}
