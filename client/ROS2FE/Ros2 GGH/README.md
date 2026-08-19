# Ros2 GGH — 机器人远程操作控制台

ROS2 机器人（Nav2 + rosbridge）的 Web 远程操作项目：后端 C.WebApi 将 ROS 话题桥接为 REST / SignalR（MessagePack 二进制帧）/ SRS WebRTC 视频，前端使用 Nuxt 4 构建单页控制台。

## 目录结构

```text
Ros2 GGH/
├── CLAUDE.md                 # 项目工作约定（文档同步、设计规范）
├── README.md                 # 项目说明（本文件）
├── zero.md                   # 更新日志（时间 + 更新内容）
├── interface.md              # ROS2 话题接口契约（rosbridge）
├── Design/
│   ├── modern.md             # 现代简约设计规范
│   └── console-hud.md        # 控制台 HUD 设计方案（透明叠印 + 开放式取景框）
├── Backend/                  # C.WebApi 前端接入交付物（无后端源码）
│   ├── C.WebApi-前端接口与实时通信接入文档.md
│   ├── C.WebApi.postman_collection.json
│   ├── C.WebApi.local.postman_environment.json
│   ├── cwebapi-client.js     # 浏览器 ESM SDK（不绑定框架）
│   ├── cwebapi-client.d.ts
│   └── postman/              # Postman Local View 工作区资源
├── Robot/
│   └── 通信脚本/             # 直连机器人 rosbridge 的 Python 样例与话题接口
│       ├── video.py          # 订阅 /aurora/rgb/image_raw 显示 BGR8 图像
│       ├── move.py           # 发布速度并订阅 /cmd_vel_safe
│       ├── keyboardmoving.py # 20Hz 键盘控制
│       ├── interface.md      # ROS2 话题契约
│       └── 通信接口.md       # 话题中文速查
└── Frontend/                 # Nuxt 4 前端（单页控制台）
    ├── nuxt.config.ts
    ├── package.json
    ├── components.json       # shadcn-vue 配置
    ├── tsconfig.json
    └── app/
        ├── app.vue           # 首页：导航、认证状态、视图切换与页脚
        ├── assets/css/main.css   # Tailwind v4 入口 + 设计令牌（亮/暗主题）
        ├── composables/
        │   ├── useAuth.ts        # 标签页会话恢复、认证 API 与 Token 到期处理
        │   ├── useRobot.ts       # 操作者状态/门控 + CommandHub 按需 15Hz 控制循环
        │   ├── useTelemetry.ts   # MapHub 地图/遥测、帧频诊断、CameraImage 与连接生命周期
        │   └── useLanCamera.ts   # 临时直连 rosbridge 的 RGB 图像订阅与重连
        ├── components/
        │   ├── AuthDialog.vue    # 登录、注册、邮箱验证与密码找回弹窗
        │   ├── OverviewView.vue  # 操作席简报：身份、现场视频、电量与最近会话链路概况
        │   ├── OverviewVideoPreview.vue # 概览单一 SRS WHEP 视频预览与帧状态
        │   ├── ConsoleView.vue   # 控制台 HUD 编排（视频 / 态势 / 感知舞台 + 三模式抽屉）
        │   ├── console/          # 控制台 HUD 与媒体舞台组件
        │   │   ├── KeyboardControls.vue # 桌面键盘映射与实时按压反馈
        │   │   ├── JoystickPad.vue      # 移动端虚拟摇杆
        │   │   ├── EStopButton.vue      # 移动端急停按钮
        │   │   ├── DiagnosticsPanel.vue # 连接、地图帧、媒体性能与最近错误
        │   │   ├── OperatorPanel.vue    # 当前操作权查看、管理员移交与回收
        │   │   ├── VideoStage.vue       # SRS / Rosbridge / 局域网三来源视频舞台 + 实时 FPS
        │   │   ├── MapStage.vue         # 覆盖范围态势场、原始解析与 RTS 借鉴式导航交互
        │   │   └── PointCloudStage.vue  # PointCloudHub + Three.js 近距感知 / 空间扫描舞台
        │   ├── UiTooltip.vue     # Reka UI Tooltip 统一悬浮提示
        │   └── ui/button/        # shadcn-vue Button 组件
        ├── plugins/signalr.client.ts # 挂载 SignalR 运行时
        └── lib/
            ├── cwebapi/          # 与 Backend/ 同步的 SDK 副本（含 MapHub 帧解码错误回调）
            ├── map-snapshot-cache.ts # IndexedDB 静态地图快照（本地观测回退）
            ├── whep-player.ts    # 内网 WHEP 接收与签发地址归一化
            └── utils.ts          # cn() 类名工具
```

## 前端

- **技术栈**：Nuxt 4 · Vue 3 · TypeScript 6.0.3（vue-tsc 3.3.9 兼容基线）· Tailwind CSS v4 · shadcn-vue（Reka UI）· pnpm（Node ≥ 22.19）
- **动画分层**：CSS 过渡 + Vue 内建 `<Transition>` 打底（微交互）→ **motion-v** 补充（组件级弹簧物理）→ **GSAP** 主力（时间线编排、SVG、数字滚动）；所有入场动画遵循 `prefers-reduced-motion` 降级
- **控制台专用**：Three.js（点云渲染）· 虚拟摇杆为原生 Pointer Events 实现（nipplejs 留作移动端备用）· @nuxt/icon（Iconify，本地装有 `lucide` 与 `game-icons` 图标集；静态扫描及登录相关动态图标均预打包，首屏不依赖外部 Iconify 请求）
- **概览入口**：首页首次打开时不建立 MapHub 或 PointCloudHub；完成认证且具备 `stream.play` 权限时，概览只按需建立一条轻量 SRS WHEP 视频预览，不再读取地图 Canvas 或地图快照作为主视觉。未认证、游客或无推流时显示明确的锁定/空状态；点击视频区域进入控制台视频视图。控制台首次进入后会在概览/控制台切换间保留单一缓存实例：控制台停用时仅暂停点云 GPU 更新、地图重绘与控制台视频帧采样；概览视频在激活时独立建立、离开时释放，保证不会同时占用两条 WHEP 接收器；登出或页面最终卸载才执行完整清理。桌面端导航与概览共用 `1440px` 阅读宽度，视频场景按剩余视口高度弹性铺开；运行快照在内存中即时更新，存储层合并为最多约每秒一次写入，控制台真正卸载时会落盘最后一帧；最多保留 30 分钟，明确标记为 `LAST SESSION`，不会伪装为在线真值，也不能解除导航门控；窄屏顶部保留一个图标化控制台入口，仍先经过认证门控。
- **Tooltip**：概览的身份、视频状态、运行快照与紧凑链路状态使用统一 Reka UI Tooltip，支持键盘聚焦与 reduced-motion；触摸设备依赖可见文字，不把 Tooltip 当作告警通道。
- **shadcn-vue**：配置见 `components.json`，组件放 `app/components/ui/`（shadcn-nuxt 无前缀自动导入）；CLI 卡住时可参照 `ui/button/` 手写组件，注意 props 不能直接 `extends` 包内导入类型（`@vue/compiler-sfc` 限制）
- **开发**：`cd Frontend && pnpm install && pnpm dev`，访问 `http://localhost:5173`
- **局域网联调**：浏览器默认直连 `http://192.168.1.139:5122`，开发站点固定为 `http://localhost:5173` 以匹配后端 CORS 白名单并支持 SignalR WebSocket。可用 `NUXT_BACKEND_BASE_URL` 临时切换后端；后端签发 `localhost` / `127.0.0.1` WHEP 地址时，前端自动保留端口、路径与 Token 并将主机替换为后端主机，也可用 `NUXT_SRS_BASE_URL` 显式指定 SRS Origin；机器人直连当前默认使用 `ws://192.168.1.30:9091` + `/aurora/rgb/image_raw`，可由 `NUXT_ROSBRIDGE_CAMERA_URL` / `NUXT_ROSBRIDGE_CAMERA_TOPIC` 覆盖
- **2026-08-04 连通实测**：C.WebApi `/health`、`/ready`、游客认证及 MapHub / PointCloudHub negotiate 首轮均成功；MapHub 随后真实收到 `169×93 / 0.05m` 地图、里程计、电量与电压帧，证明机器人→后端→SignalR→前端链路可工作。机器人 `9090` 曾拒绝连接后恢复，`9091` 全程可直连并收到 `/map`、`/odom`、`/odometry/filtered`、RGB 图像、`/bms/soc` 与 `/bms/voltage`，故局域网摄像头默认改用 `9091`；`/scan`、`/aurora/points2`、路径与代价点云本轮无帧。末次复测时后端 `5122` 已超时且 MapHub 以 WebSocket 1006 断开，当前仍需排查后端进程或网络的间歇性退出
- **2026-08-07 点云联调**：管理员与游客 Token 的 `/api/pointcloud/streams`、`/api/devices` 连续请求均为 `200`，浏览器跨域 Bearer 请求也成功；PointCloudHub WebSocket 可连接并订阅，但后端当前仅返回 `default → /points (sensor_msgs/msg/PointCloud2)`，8 秒内未发出任何帧。前端严格以 API 元数据为准，不会把流名强行改为 `/scan`；需由后端将流配置指向真实发布话题，或由机器人侧恢复 `/points` 发布。感知舞台在订阅后 3 秒仍无首帧时显示 `HUB · NO FRAME`，诊断中心显示 `NO FRAME`，与鉴权失败、空回波和 `FRAME HOLD` 分开。
- **认证**：支持用户名/邮箱登录、注册、邮箱验证与重发、游客登录、退出、忘记/重置密码；公共认证请求超时为 60 秒，以容忍局域网认证实例的慢响应；Access Token 与无密码会话元数据仅保存于同一浏览器标签页的 `sessionStorage`，刷新页面会恢复，关闭标签页、主动退出、令牌到期或损坏数据会清除；后端没有 Refresh Token API。认证客户端的 `401` 只会清理与其创建时相同的 Token，旧请求不能清掉后来重新登录的会话。**2026-08-07 实测当前后端采用同账号单会话**：同一 `admin` 再次登录后，旧 Token 的受保护请求会由 `200` 变为 `401`；因此 Postman、公网测试页或其他标签页用同账号重新登录，会让本页在下一次 API / Hub 请求时合理地要求重新登录。
- **控制权限**：登录后查询 `/api/operator/current`；非游客通过 ChatHub 监听 `OperatorChanged`，仅 `isSelf=true` 建立 CommandHub。手动速度循环只在 WASD / 摇杆存在有效输入时启动，最后一个输入释放时停止循环并发送一次 `StopRobot`；空闲、纯导航、连接建立和无手动驾驶的页面退出均不向高优先级 `/manual_cmd_vel` 持续注入零速，避免覆盖 Nav2
- **操作者面板**：右侧抽屉独立显示 `OPEN / HELD / CURRENT` 操作权状态；首次状态查询同样适用于游客，但游客不建立 ChatHub。非管理员仅查看；具备 `system.admin` 的账户可手动读取可分配账户，并在 4 秒内再次点击确认后通过既有 REST 接口移交或回收操作权。面板只展示用户名和状态、不展示邮箱，不与诊断中心混放；它复用现有 `OperatorChanged` 状态，不建立第二条 ChatHub，后端权限校验仍是最终约束
- **组合键**：`W+A` / `W+D` 分别提交前进左转 / 前进右转，`S+A` / `S+D` 分别提交后退左转 / 后退右转；线速度轴与角速度轴独立合成。组合状态持续输出同一条非零 `linearX` 与 `angularZ`：常规线速度为 `±0.5 m/s`，角速度从 `±0.1 rad/s` 平滑升至 `±0.5 rad/s`；按住 `Shift` 后线速度目标为 `±1.0 m/s`、角速度目标为 `±1.0 rad/s`。同轴反向组合（`W+S`、`A+D`）抵消后不再维持零速度循环，避免把“有按键”误判为有效手动驾驶
- **响应式控制**：桌面端（宽度 `>720px`）只显示 `W/A/S/D`、`Shift`、`Space` 键帽映射，真实按键时同步下沉、点亮、显示速度档位并让急停键短促警示；移动端（宽度 `≤720px`）只显示虚拟摇杆和独立急停按钮；无控制权时两套控件均锁定
- **速度档位**：`W/S` 常规线速度 `0.5 m/s`，按住 `Shift` 或摇杆满量程时前端请求 `1.0 m/s`；键盘 `A/D` 首帧为 `±0.1 rad/s`，约 `0.5s` 平滑升至常规上限 `±0.5 rad/s`，Shift 时平滑趋近 `±1.0 rad/s`；当前后端接口文档仍声明最大线速度钳制为 `0.5 m/s`，实车达到 `1.0 m/s` 前必须同步后端安全配置
- **控制台抽屉**：右侧 `HudPanel glass` 使用“设置 / 操作者 / 诊断”三标签；设置承载当前视频、地图或点云工具，操作者页承载当前操作权与受限管理动作，诊断汇总公共运行状态。收起静止态仅显示 38px 透明贴边导轨并关闭模糊，悬浮/键盘聚焦时激活导轨、状态点和箭头，点击后以 340ms 双轴展开至最多 320px；悬浮、聚焦或展开期间暂停闲时隐藏，离开且收起 3 秒后 HUD 完全隐藏
- **会话级 UI 状态**：使用 Nuxt `useState` 在当前页面会话内保留抽屉页签、视频来源、地图图层显隐与态势场开关，以及点云首选流、点大小、显示上限、着色、网格/坐标轴和导出格式；切换舞台后恢复这些偏好，刷新页面后回到默认值。概览/控制台切换只缓存一个控制台实例，后台不接收键盘驾驶输入；地图、视频和点云分别暂停重绘、帧采样和 GPU 缓冲消费，重新进入时恢复当前会话状态。地图的“定位已人工确认”仅是本次页面会话的安全门控，绝不由 `RobotPose` 自动推断，也不代表 ROS/AMCL 的真实置信度；当前操作者失权时该门控与本地选中状态会强制复位。操作者账户列表和二次确认不会持久化。暂停态、错误、轨迹、导航目标和远端任务状态均不持久化；受控例外是无密码的 Access Token 会话写入标签页 `sessionStorage`、静态 `Map` 写入作用域化 IndexedDB、点云保留受显示上限约束的 `useState` 缓冲，三者都不代表实时数据或控制真值
- **前端诊断中心**：不新建后端连接，直接汇总 API、CommandHub、MapHub、视频和 PointCloudHub 状态；Map / RobotPose / Scan / Path / LocalPlan / Particles / Costmap / MapPatch 记录到达时间、累计帧数和 EWMA 频率。当前连接收到的静态地图标为 `STATIC`，从本机恢复的地图标为 `CACHED`；两者都不会因长期不更新误报陈旧。Map 帧解码失败及连接后一次缓存补订阅仍未收到 Map 的状态会进入 MapHub 最近错误。视频与点云均显示 `FRAME HOLD`；点云的会话级恢复帧单独显示 `CACHED`，不会伪装为 FPS 或在线状态。均保留最近 6 条当前会话错误，可下载不含 Token 的本地 JSON 快照
- **沉浸式角落控件**：左上退出采用 40px 透明命中区内的箭头与开放角线，点击与 `Esc` 等效；右上 PiP 使用 `HudPanel bare` 四角取景框与稀疏点阵，已移除整面 `.hud-panel-bd`、玻璃背景和模糊，仅右侧控制台抽屉保留玻璃材质
- **视频流**：抽屉提供三种互斥来源：`SRS` 使用后端签发的 WHEP URL 和 WebRTC；`Rosbridge` 使用 C.WebApi MapHub 中转的 `CameraImage` BGR8 帧；`局域网` 按 `Robot/通信脚本/video.py` 从浏览器直连机器人 rosbridge。切换时先释放旧 RTCPeerConnection/WebSocket/帧超时，再启动新来源，选择会在控制台视图切换后保留；主舞台以低权重源名、实际帧分辨率、状态与左下 FPS 建立观察读数。SRS 只有视频元素实际收到可播放帧后才标记 `LIVE`，并使用 `requestVideoFrameCallback`（不支持时采样累计解码帧增量）统计真实接收帧率；每次 WHEP 启动都以本地代际隔离旧的签发地址、PeerConnection、媒体轨道和帧率回调，过期会话不得清空新画面或覆盖新状态。三来源连续 2.5 秒没有真实新帧时保留最后画面并标记本地 `FRAME HOLD`，新帧到达后才恢复 `LIVE`。保持态不等于视频服务、MapHub、ROS 或机器人仍在实时在线
- **视频网络边界**：WHEP/WebRTC 媒体不经过 SignalR；Rosbridge 档经过 C.WebApi MapHub SignalR；局域网档不经过 C.WebApi。SRS 内网 WHEP 通道现已由用户确认通信正常；前端仍区分“信令/ICE 已建立”和“视频元素收到媒体帧”，实际 FPS 数值与推流稳定性由用户手动验收
- **地图与导航**：`useTelemetry.ts` 只建立一条共享 MapHub MessagePack 会话，`MapStage.vue` 消费 `Map`、`RobotPose`、`Particles`、`Path`、`LocalPlan`、`Scan`、`GlobalCostmap`、`LocalCostmap`、`MapPatch` 九类帧；Canvas 按 ROS 世界坐标绘制并垂直翻转 y 轴，支持自动适配、中键按住拖拽平移、滚轮缩放、机器人跟随和最多 600 点的行驶轨迹，中键拖拽/缩放会退出跟随。自动重连只清理动态遥测并保留已验证静态 Map，断线期间明确转为本地 `CACHED` 观察态；首次订阅或重连后 1.5 秒仍无新的 Map 时，只额外补订阅一次请求后端缓存。图层状态点按共享帧时钟显示绿色新鲜、琥珀色陈旧或灰色缺失，悬停可读取明确状态；RobotPose/Scan 为 2.5 秒，LocalPlan 4 秒，Particles 6 秒，Path 8 秒，Local/Global Costmap 为 5/12 秒，MapPatch 为 15 秒，静态 Map 不参与陈旧判断。定位流程为“初始位姿草案 → `SetInitialPose` 成功 → 人工确认 → 导航就绪”，`RobotPose` 不能自动解除该门控。桌面浏览态支持左键点选机器人、中键按住拖拽平移、约 280ms 长按左键后拖拽框选；定位就绪后右键短点会以机器人至落点方向为默认朝向，右键拖拽可精确设朝向，并在松开时单线程提交 `SetGoal`。`Shift + 右键`只保留本地草案，因为接口尚未提供队列生命周期；`Esc`只取消本地草案/框选，不能取消已提交的 Nav2 任务。画布以蓝色选中角标、青绿初始位姿罗盘、琥珀目标准星和红色无效落点呈现世界坐标反馈，落点环仅持续 160–220ms 并支持 reduced-motion 降级。重复目标在短窗口内去重。目标提交成功后本地绘制目标方向、机器人连线与距离，并且只以“已提交 / 检测到移动 / 接近目标 / 疑似停滞”标记为“前端估算”。“停止追踪”只清除本地叠加，不等于取消导航；当前接口仍无任务 ID、Nav2 反馈、取消和定位置信度。后端未携带叠加层 `frame_id`，局部层与扫描的空间对齐仍需真机核对
- **地图本地快照（仅控制台）**：每次收到真实 `Map` 帧后，前端以“后端地址 + 当前登录身份”作为作用域，把经尺寸和二进制长度校验的静态栅格写入浏览器 IndexedDB；快照有效期最多 7 天，并以内容指纹与 2 秒节流避免高频重复写入。下一次进入控制台或 MapHub 连接未完成时，先显示最近一份 `CACHED` 地图和帧龄；概览主视觉不再使用这份地图快照，只恢复 `MapFrame` 本体，不恢复 RobotPose、路径、扫描、代价图、导航草案或认证信息。缓存地图仅可观察、平移和缩放，必须等当前连接收到新的真实 `Map` 帧后才允许 `SetGoal` / `SetInitialPose`；退出登录会清除该身份的本地地图快照。当前 MapHub 契约没有设备 ID，第一版不能跨多设备复用同一后端地址下的快照
- **地图观察预设与落点保护**：抽屉提供会话级“态势 / 解析”预设；态势默认保留环境、任务路径、即时路线与行驶轨迹，解析才打开路径风险、近场风险、扫描、定位分布和环境变化，手动开关图层后显示 `CUSTOM`。目标草案和提交都会检查静态 Map 占据栅格，值 `≥65` 时在前端阻止明显障碍落点；未知栅格不假定不可达，最终可达性仍由后端导航栈判断
- **地图渲染性能**：原始静态 Map 位图、态势环境证据位图、全局/局部代价图与地图增量分别缓存；MapHub 帧只标记对应图层脏状态，同一 `requestAnimationFrame` 合并最新帧。Map 以几何签名和内容哈希判定是否重建：地图内容未变时不会重新生成位图；地图原始几何变化时才重新适配视图，场景框也不会因每帧重复入场。扫描与粒子跨整帧均匀抽样，最高分别绘制 2,800 / 1,800 点；全局路径与局部规划分别限制为最多 4,000 / 2,400 个显示点，保留 NaN 分段断开及每段首尾点，局部规划标记批量填充。动态代价图、路径、局部规划、扫描、粒子与地图增量跨过既有陈旧阈值后以 34% 透明度降权显示，新帧立即恢复；静态 Map、态势证据和本地行驶轨迹不受影响
- **地图态势显示层**：默认“态势”以 `MapFrame` 原始数据生成一层低噪声环境证据：已知自由区域用浅色覆盖、未知区域保持透明、占据值用深色证据标记。该层只做逐格着色和低对比呈现，不闭合边界、不补齐未知格、不生成墙线/自由区/障碍轮廓，也不把碎片伪装成建筑图。覆盖范围、米制参考网、虚线范围边与四角标记仍只表达地图坐标的可观察范围；蓝色计划路径、青绿即时路线、低对比虚线行驶轨迹、机器人定位环与目标仍使用真实世界坐标。无已知栅格时场景上下文和 Canvas 场内状态显示 `NO ENV EVIDENCE`，用于区分“地图帧存在但没有可呈现环境证据”和连接失败；态势分支不等待原始栅格位图重建，因此首帧也能显示范围与状态。全局代价为琥珀色路径风险，局部代价为红色近场风险，`MapPatch` 仅为青绿环境变化叠层。“解析”保留原始像素栅格、工程网格和诊断图层；关闭“态势场”同样恢复原始像素采样。展示层不修改 `MapFrame.data`，不参与 `mapCellState()`、落点拦截、`SetGoal` 或 `SetInitialPose`
- **地图指针层**：地图画布始终使用系统原生光标，不再由 Canvas 绘制或隐藏屏幕级指针。机器人选中框、初始位姿草案、导航目标和提交反馈仍锚定 ROS 世界坐标；选中、初始位姿、导航和阻止反馈分别限定为 160ms、220ms、180ms、120ms 的一次性效果，支持 reduced-motion 降级
- **点云**：只订阅 `/api/pointcloud/streams` 返回的流名，通过 PointCloudHub + MessagePack 接收帧并写入 Three.js 动态缓冲；网络帧在同一 `requestAnimationFrame` 内只消费最后到达的一包，数据 FPS 仍按全部入站帧统计。首次默认优先选择机器人 `/scan`（兼容流名 `scan`），缺失时回退到 `points2` 或后端首个流，之后在页面会话内优先恢复用户选择。显示上限使用跨整帧均匀采样的 LOD，恢复或切流时会按当前流 `maxPoints` 重新钳制；支持点大小、单色/高度/距离着色、网格和坐标轴开关、视角适配、冻结/恢复当前帧、数据/渲染 FPS，以及当前可见点的 ASCII PLY / CSV 导出。单色使用 Three.js 材质统一上色，不逐点写颜色缓冲；高度/距离模式才使用顶点颜色。冻结只停止画面更新，仍监测数据链；暂停、切流、重连和卸载均撤销待消费帧，避免旧流写回；不做缺少 TF 时会错位的多帧累积。订阅后 3 秒仍未收到第一帧时显示 `HUB · NO FRAME`，不把它当作登录失效或渲染故障；收到首帧才进入 `LIVE`；连续 2.5 秒未收到新帧时保留最后一帧并标记本地 `FRAME HOLD`，新帧到达后才恢复 `LIVE`。返回概览时缓存现有 WebGLRenderer，仅暂停 rAF 与 GPU 缓冲消费；重新进入控制台才消费最新一帧，不在视图切换中销毁或重建上下文。正常舞台切换只释放 Three.js 资源，不主动丢失 WebGL 上下文；若创建、Resize、渲染或上下文自身丢失失败，前端立即停止点云连接和绘制、显示本地错误，并在 15 秒冷却内拒绝再次创建，避免浏览器卡顿或卡死。切流和真正卸载释放 SignalR 订阅、OrbitControls 与 GPU 缓冲
- **点云会话快照**：每个真实可见帧都会只保留一份已筛选、已 LOD 的位置/颜色缓冲，点数仍不超过当前显示上限；在相同登录身份和相同流下切回感知舞台或短暂重连时可先恢复为蓝色 `CACHED`。这份快照只存在当前页面会话内，不写入 IndexedDB、不会恢复原始 `xyz` 全量帧、不会生成数据 FPS，也不会代表 PointCloudHub 或传感器在线；首个真实帧到达立即改回 `LIVE` 并覆盖快照，切流或退出登录会清除。需要跨刷新保存时仍使用现有 PLY / CSV 主动导出
- **点云观察与质量**：观察选项默认按流自动匹配：`/scan` 使用俯视，`points2` 与其他三维流使用透视视角；用户拖动相机后显示 `CUSTOM`。距离与高度筛选均默认关闭，打开后从实际消费的同一帧统计原始、有限、筛后候选和最终可见 LOD 点；筛选参数在单帧处理前只读取一次，范围比较使用平方距离，完整筛后范围用于稳定自动适配与高度/距离着色，GPU 只上传实际可见 LOD 的属性区段。抽屉与诊断中心同步展示 `VALID`、`CUT`、`DRAW IDLE`，导出的 PLY/CSV 写入流名、话题、筛选范围、视角、原始/有限/筛后/可见点数等本地元数据。暂停时参数变更只重算冻结的可见帧。静态相机且无新帧时 Three.js 不维持连续渲染循环，数据到达、用户交互、尺寸或显示参数变化才按需重绘
- **点云场景呈现**：`/scan` 使用 `PERIMETER SENSE` 近距感知，其他流使用 `SPATIAL SCAN` 空间扫描；首屏默认关闭网格、坐标轴与背景工程格。扫描俯视时以随可见范围缩放的三层本地感知环、边缘刻度和原点环建立距离感，切到三维或自定义视角后恢复用户的辅助线偏好。首帧等待、空回波、筛选后为空、`FRAME HOLD` 与连接错误各自呈现明确状态；保持态只供观察最后画面，不等于传感器、地图或机器人仍在实时更新。参考环只表达传感器局部空间，不与地图硬融合，也不伪装为全局三维场景
- **电池遥测**：登录（含游客）后连接 MapHub（MessagePack）并调用 `Subscribe()`，接收 `BatterySoc` / `BatteryVoltage`；SOC 按 `0-100` 钳制，并经 5 点中值滤波、指数低通和显示死区抑制负载波动，电压使用同类轻量平滑；断线清空旧值，电量条保留填充过渡、数值切换、高光扫过、低电量警示及 `prefers-reduced-motion` 降级
- **顶部运动遥测**：同一 MapHub 会话接收 `Odometry` 与 `SafeTwist`；顶部数据带在里程计存在时标记 `ODO`，显示其 X / Y / θ / V / Ω，缺失时标记 `SAFE` 并仅以安全控制输出兜底 V/Ω。`ODO` 是里程计坐标，不得替代地图 `RobotPose`；左下 `CMD` 则明确是前端请求线速度/上限。运动帧最高以 10Hz 写入 Vue 状态，2.5 秒无新帧回到 `--`，重连和退出均清空旧值
- **构建 / 预览**：`pnpm build` / `pnpm preview`；类型检查 `pnpm typecheck`
- **当前验证约定**：日常迭代只复用 `http://localhost:5173` 做开发态检查并按需运行 `pnpm typecheck`；不自动执行生产构建，最终联调由用户手动验收。2026-08-04 本阶段已通过类型检查与 1280×720 开发页检查，地图/Three.js 画布截图和像素均非空；本次另验证抽屉、地图和点云偏好可跨舞台重挂恢复且硬刷新回默认，页面无新增运行时错误。当次连接收到 `169×93 / 0.05m` 静态地图，但 RobotPose 与点云首帧未到，相关动态行为保留手动实机验收
- **当前进度**：认证、响应式控制、控制台设置/操作者/诊断抽屉、三来源视频、实时视频 FPS、MapHub 地图、定位门控与 RTS 借鉴式单目标交互、PointCloudHub 可调点云、电池平滑及顶部运动遥测均已接入；操作者面板已复用现有状态和管理 REST 契约，权限与实际移交结果待用户联调确认。用户此前已确认 SRS、`/scan` 点云和导航链路可用。已修复空闲手动速度源覆盖 Nav2；当前前端可继续在现有后端契约上做显示与诊断，但 Nav2 真实任务状态/取消/队列、定位置信度、TF/frame_id 和跨帧点云融合仍需后端或机器人侧新增契约
- **当前场景化阶段**：主舞台已收束为“视频 / 态势 / 感知”。态势层以原始栅格的环境证据着色补足空态可读性，但不把低质量栅格伪装成建筑图；感知层只负责让单帧近距空间更可读。二者都不新增后端连接、不写回原始帧，也不在缺少可靠 `frame_id` / TF 时宣称跨源融合

## 设计规范

视觉遵循 `Design/modern.md`（现代简约）：留白优先、中性色为主 + 单一强调色（`#2563eb`）、1px 细分割线、小圆角（8px）、轻量阴影、8px 间距基数。设计令牌以 CSS 变量集中在 `Frontend/app/assets/css/main.css`（命名对齐 shadcn-vue 约定：`--background` / `--foreground` / `--primary` / `--border` 等，并通过 `@theme inline` 映射为 Tailwind 工具类），另含控制台用暗色主题占位值（`.dark` 作用域，未定稿）。

## 接口与契约

- **ROS2 话题**：见 `interface.md`（原始样例端口为 `ws://192.168.1.30:9090`；当前前端局域网摄像头实测默认 `ws://192.168.1.30:9091`，可由环境变量覆盖；控制推荐 `/manual_cmd_vel`）
- **机器人直连样例**：见 `Robot/通信脚本/`；`video.py`、`move.py`、`keyboardmoving.py` 均直接访问机器人局域网 rosbridge，不经过 C.WebApi；前端临时视频源复用 `video.py` 的订阅消息与 RGB/BGR 解码规则，地址以当前运行时配置为准
- **后端 API**：见 `Backend/C.WebApi-前端接口与实时通信接入文档.md`（当前局域网联调地址 `http://192.168.1.139:5122`；4 个 SignalR Hub；无 Refresh Token）
- **前端 SDK**：已接入 —— 副本在 `Frontend/app/lib/cwebapi/`（勿改，随 `Backend/` 原件更新），`plugins/signalr.client.ts` 挂载 `globalThis.signalR`（signalr 8.0.7 + msgpack）；开发环境从 `localhost:5173` 直连局域网后端，REST 与 SignalR 共用 `runtimeConfig.public.apiBase`

## 文档约定

每次代码更新后：同步更新相关文档，并在 `zero.md` 追加一条「时间 + 更新内容」记录，同时保持其顶部目录结构最新。详见 `CLAUDE.md`。
