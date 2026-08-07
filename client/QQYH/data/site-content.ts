export type SiteNavItem = {
  label: string
  to: string
}

export const company = {
  name: '湖南桥桥友河智能装备有限责任公司',
  shortName: '桥桥友河智能装备',
  englishName: 'QQYH INTELLIGENT EQUIPMENT',
  address: '湖南省衡阳市衡山县开云镇兴园路智能产业制造园 A1-4 栋',
  postalCode: '421342',
  registrationDate: '2025年7月18日',
  creditCode: '91430423MAER1DJA0A',
  buildingName: '桥桥友河创新中心办公楼',
  contact: {
    phone: '18942002446',
    email: '3593997272@qq.com',
    businessHours: '8:00 - 18:30',
  },
  registration: {
    capital: '500万元人民币',
    status: 'xxx',
    type: '智能装备|机械制造',
  },
  filing: {
    icp: 'xxx',
    publicSecurity: 'xxx',
  },
  location: {
    region: '湖南省 · 衡阳市 · 衡山县 · 开云镇',
    road: '兴园路',
    park: '衡山智能制造产业园（智能产业制造园 A1-4 栋）',
    industry: '园区以智能制造为主导方向，并布局新能源、新材料产业。',
    surroundings: '开云镇为衡山县城所在地，毗邻南岳衡山风景区。',
    access: '临近衡山西站，周边可衔接京港澳高速（G4）与衡邵高速等交通网络。',
  },
  statement: '面向工程建设场景，连接机械装备、自动化控制与数字化系统。',
  description:
    '湖南桥桥友河智能装备有限责任公司立足衡阳衡山智能产业制造园，围绕工程装备制造、机械系统集成、软件开发及设备安装运维，构建从现场需求到装备交付的协同能力。',
  locationSummary:
    '桥桥友河创新中心办公楼位于湖南省衡阳市衡山县开云镇兴园路衡山智能制造产业园 A1-4 栋，地处衡山县高新区核心地带。该园区是衡山县智能制造产业的主阵地，交通便利（靠近衡山西高铁站），产业配套逐步完善，属于湖南省“中国制造2025”试点示范城市群的重要组成部分。企业以预应力智能装备研发制造为核心，是衡山智能制造产业园的重点入驻企业之一。',
} as const

export const siteNavigation: SiteNavItem[] = [
  { label: '首页', to: '/' },
  { label: '关于我们', to: '/about' },
  { label: '望衡 HENGVIEW', to: '/hengview' },
]

export const serviceAreas = [
  {
    index: '01',
    title: '装备与系统集成',
    text: '依生产流程，组织机械结构、执行机构、控制与信息系统。',
  },
  {
    index: '02',
    title: '软件与运行维护',
    text: '软件开发与系统集成，兼顾交付后的长期运行维护。',
  },
  {
    index: '03',
    title: '安装与现场服务',
    text: '按项目范围承接设备安装、调试与交付协同。',
  },
] as const
