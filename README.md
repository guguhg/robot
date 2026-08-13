# 巡检机器人（Smart Inspection Robot）

RK3568 + Jetson Nano 双板架构的工业巡检机器人，覆盖从 EtherCAT 实时伺服控制到 ROS2 自主导航的完整嵌入式软件栈。

## 架构

| 模块              | 目录          | 硬件平台              | 技术栈                                                       |
| ----------------- | ------------- | --------------------- | ------------------------------------------------------------ |
| 大脑（感知/决策） | `brain/`      | Jetson Nano           | ROS2 · EKF 定位 · SLAM 建图 · Nav2 导航 · YOLO 视觉          |
| 小脑（实时控制）  | `cerebellum/` | RK3568（野火鲁班猫2） | PREEMPT-RT · IgH EtherCAT · ros2_control · 雷赛 L7EC-400S 伺服 |
| 前端              | `client/`     | —                     | Nuxt · Web 监控                                              |
| 系统脚本          | `scripts/`    | —                     | 配网 · frp 内网穿透 · RTMP 推流                              |

## 核心亮点

- **1ms 周期实时控制**：抖动标准差 86.6μs → 6.3μs（13.7 倍），最大抖动 10ms → 308μs（32.5 倍），通过系统级（PREEMPT-RT + CPU 隔离）+ 进程级（绑核 + SCHED_FIFO）+ 代码级（内存锁定 + 绝对时间）三层优化实现
- **共享内存零拷贝 IPC**：双缓冲 + 原子索引 + seq 校验，打通 IgH 主站与 ros2_control 进程，避免互斥锁优先级反转
- **自研控制流仲裁器 + 多边形空气墙**：替代官方 cmd_vel_mux / nav2_collision_monitor，多速度源按优先级仲裁，四方向多边形点云检测限速
- **EKF 融合定位**：IMU 角速度 + 轮式里程计，转弯信 IMU、直行信 Odom
- **通信三流分离**：视频（RTMP→SRS→WebRTC）/ 话题（rosbridge + frpc）/ 控制（MQTT）独立通道，互不干扰

## 目录说明

```
robot/
├── brain/          # 大脑：ROS2 感知与导航（详见 brain/README.md）
├── cerebellum/     # 小脑：EtherCAT 伺服控制 + ros2_control（详见 cerebellum/README.md）
├── client/         # Web 前端监控界面
├── cloud/          # 云端服务
└── scripts/        # 系统服务脚本（配网、内网穿透、推流）
```

## 数据流概览

```
/scan + /odom + /imu → EKF 定位 → SLAM/Nav2 导航 → /cmd_vel
  → 控制流仲裁器 → 空气墙防撞 → /cmd_vel_safe
  → ros2_control（姿态补偿 + IK）→ 共享内存 → EtherCAT 进程 → 雷赛伺服
```
