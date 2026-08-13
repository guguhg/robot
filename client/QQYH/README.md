# 桥桥友河企业官网前端

湖南桥桥友河智能装备有限责任公司官网与望衡 HENGVIEW 3D 展厅前端。基于 Nuxt 4、Vue 3、TypeScript、Three.js、GSAP 和 Lenis，使用 pnpm 管理依赖。

本文件只描述 Nuxt 前端实现。项目交付总览（项目介绍、目录结构、技术栈、构建预览、资源约定与完整更新记录）见同级 `阅读我.md`。

## 页面与入口

```text
pages/
|- index.vue              企业官网首页
|- about.vue              关于我们
|- hengview.vue           望衡 HENGVIEW 3D 展厅
`- [...slug].vue          未匹配路径的 HTTP 404
```

正式路由只有 `/`、`/about` 和 `/hengview`，其他路径返回 404。导航与企业资料来自 `data/site-content.ts`，不在页面组件中维护第二份公司信息。

## 安装

运行要求：

- Node.js `^22.18.0 || >=24.11.0`
- pnpm `11.x`，当前固定版本为 `11.13.1`（由 `package.json` 的 `packageManager` 字段锁定）

```powershell
Set-Location E:\0zero\desktop\桥桥友河企业官网前端\Front
pnpm install --frozen-lockfile
```

仓库只使用 `pnpm-lock.yaml`。不要混用 npm、yarn 或生成第二份锁文件。

## 开发、构建与预览

```powershell
# 开发服务器
pnpm dev --host=127.0.0.1

# 静态维护检查
pnpm typecheck
pnpm verify:hengview

# 生产构建与本地预览
pnpm build
pnpm preview --host=127.0.0.1

# 静态站点输出（临时上线采用，产物在 .output/public/）
pnpm generate

# 静态产物预演（从 Front/ 目录执行）
pnpm dlx serve .output/public
```

Nuxt 默认监听 `http://127.0.0.1:3000/`。项目未配置单元测试和 lint 脚本；不要把构建成功视作视觉验收完成。若 Nuxt CLI 将分开的 host 参数误解析为项目根目录，使用等号形式 `--host=127.0.0.1`。

## 源码结构

```text
Front/
|- assets/css/
|  |- main.css                     字体、基础样式与页面过渡
|  `- corporate.css                官网与共享壳层样式
|- assets/images/                  空目录，当前未使用
|- components/
|  |- hengview/HengviewScrollRail.vue
|  |- site/                        SiteHeader、SiteFooter、SiteShell
|  `- RouteRecovery.vue            404 视觉组件
|- data/
|  |- site-content.ts              公司资料、业务范围与导航
|  `- mzz01-mechanism-profile.ts   顶升节点和机械行程配置
|- lib/
|  |- hengview/
|  |  |- hengview-scene.ts         Three.js 场景内核
|  |  |- hengview-materials.ts     材质与 Shader 行为
|  |  `- hengview-post.ts          后处理
|  |- BlueNoiseDitherEffect.ts
|  `- BlueNoiseGenerator.ts
|- pages/                          三个正式页面与 catch-all
|- plugins/lenis.client.ts         平滑滚动插件
|- public/
|  |- media/company/               官网运行时媒体（视频、海报、图片）
|  |- media/brand/QQYH.jpg         页头品牌素材
|  |- models/mzz01-cq-explode.glb  HENGVIEW 运行时模型
|  `- favicon.png/ico              浏览器图标
|- dist -> .output/public          本机预览 Junction（不提交）
|- scripts/verify-hengview-page.mjs
|- app.vue
|- error.vue
|- nuxt.config.ts
|- package.json
|- pnpm-lock.yaml
|- pnpm-workspace.yaml
`- tsconfig.json
```

## 官网实现

- `components/site/` 为 `/` 与 `/about` 提供共享页头、页脚和页面壳层。页脚为窄条 lead、「关于企业／商务联系／网站导航」三栏目录与备案栏三层结构。
- 首页为六段单线叙事：视频 Hero、WHO WE ARE 简介、WHAT WE DO 三项能力（车间 + 产线双图）、`corp-home-base` 立足衡山（全幅建筑 + 区位事实）、`corp-home-team` 团队合照、`corp-home-horizon` 夕阳收尾（含查看企业档案 CTA）。
- 首页 Hero 按序使用 `public/media/company/hero-film-01.mp4` 至 `hero-film-06.mp4` 六段（母版顺序：公司办公楼外景(1)→工厂-03→工厂-04→工厂-01→工厂-00→QQYH-1，标签按画面内容标注：创新中心/成型工位/设备单元/车间协同/钢筋放线/品牌标识），并保留 poster、预加载、切换超时和减少动效降级；序号栏桌面与移动端均为六列。
- `/about` 为企业档案单屏卡：左列四图沉浸轮播（Ken Burns 缓推 + 交叉淡入 + 发丝进度条 + 箭头/键盘切换，悬停暂停），右列档案与商务联系（`id="contact"` 锚点）；1440x900 下整卡单屏可见，下滑即入页脚。轮播图为 `office-tower-dusk / plant-facade / park-aerial / office-signage` 四张 WebP。
- 页头 `.corp-brand__mark` 为 `public/media/brand/QQYH.jpg` 白底圆角徽标；浏览器标签图标同源（`public/favicon.png` + `favicon.ico`）。
- 公司资料单一来源为 `data/site-content.ts`：联系电话、邮箱、商务时间已使用真实资料；`filing.icp` 与 `filing.publicSecurity` 仍为 `xxx` 占位，对外正式宣传与公网部署前必须替换为已取得的备案号。

## HENGVIEW 实现

`pages/hengview.vue` 是 3D 展厅唯一页面入口，`lib/hengview/hengview-scene.ts` 是唯一场景内核。页面加载 `public/models/mzz01-cq-explode.glb`，包含装配开机、滚动镜头、顶升机构、舵轮、X-ray 和 DARK/LIGHT 展厅切换。

开发模式下页面会把场景实例挂到 `window.__hengviewScene`，便于只读诊断装配捕获；实例上的 `debugInfo` 仅供开发检查，生产构建不应依赖该句柄。正式调试入口为 `/hengview?slowboot`。

机械语义以 `data/mzz01-mechanism-profile.ts` 和当前场景代码为准：

- GLB 原始姿态是 2.0M 最高工作位，滚动机构向内套叠收拢。
- 摄像头总成、长导杆和顶节附件具有不同随动规则。
- 不要扁平化模型层级，不能仅凭视觉猜测节点与运动方向。

## 验收清单

- `/`、`/about`、`/hengview` 返回 200，随机未匹配路径返回 404。
- 桌面与 390x844 视口无横向溢出、文字遮挡或导航不可用。
- 首页视频可加载、手动切换，减少动效模式下仍可读。
- HENGVIEW Canvas 非空，加载层结束，滚动分镜、DARK/LIGHT 和返回官网入口可用。
- 浏览器控制台无页面脚本错误。
- `pnpm typecheck`、`pnpm verify:hengview` 和 `pnpm build` 全部通过。
