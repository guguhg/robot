export const MZZ01_MECHANISM_PROFILE = {
  rootName: 'MZZ01.CQ.02_顶升轴组件(2.0米）',
  stages: [
    { id: 'base', sourceName: 'MZZ01.CQ.02.01_顶升箱体（底节）', travel: 0 },
    // The retained GLB is authored at the highest working position. Travel values describe
    // how far each telescoping section retracts downward into the stage below.
    { id: 'middle', sourceName: 'MZZ01.CQ.02.02_顶升箱体（中间节）', travel: 0.26 },
    { id: 'top', sourceName: 'MZZ01.CQ.02.03_顶升箱体（顶节）', travel: 0.92 },
  ],
  topStageFollowers: ['摄像头 (1)', '镜向摄像头 (1)'],
  // These parts are authored under the middle stage in the CAD tree, but are bolted to the
  // top stage's upper surface. Match only the exact slider bodies, seats and fasteners that
  // form those rigid assemblies; the _b3 guide meshes are intentionally excluded because
  // they use the separate retractable-guide algorithm below.
  topStageSurfaceFollowerPatterns: [
    /^滑块HGH30CAZ0C(?:#2)?_b(?:1|2|4|5|6|7|8|9|10)$/,
    /^MZZ01CQ0202-24_滑块固定座（中间节）(?:#2)?$/,
    /^GB╱T_701-2008内六角圆柱头螺钉M8×35_M8×35(?:#(?:2|3|4|5|6|7|8|9|10|11|12|13|14|15|16))?_1$/,
    /^GB╱T_93-1987标准型弹性垫圈\(装配\)8_8(?:#(?:2|3|4|5|6|7|8|9|10|11|12))?_1$/,
    /^GB╱T_971-2002平垫圈_A级8_8(?:#(?:2|3|4))?_1$/,
    /^MZZ01CQ0202-15_电机吊座_Default<按加工>$/,
    /^GB╱T_701-2008内六角圆柱头螺钉M10×25_M10×25(?:#(?:2|3|4))?$/,
    /^GB╱T_93-1987标准型弹性垫圈\(装配\)10_10(?:#(?:2|3|4))?$/,
  ],
  // These two vertical guide meshes are authored at their fully exposed length. They belong
  // to the middle stage, but retract according to the top stage's travel relative to it.
  retractableGuides: [
    { sourceName: '滑块HGH30CAZ0C_b3', localAxis: 'x' },
    { sourceName: '滑块HGH30CAZ0C#2_b3', localAxis: 'x' },
  ],
} as const

export type Mzz01MechanismStageId = typeof MZZ01_MECHANISM_PROFILE.stages[number]['id']
export type Mzz01GuideAxis = typeof MZZ01_MECHANISM_PROFILE.retractableGuides[number]['localAxis']
