# 巡检机器人（Smart Inspection Robot）

RK3568 + Jetson Nano 双板架构的工业巡检机器人，覆盖从实时伺服控制到 ROS2 自主导航的完整嵌入式软件栈。

## 架构

| 模块 | 目录 | 技术栈 |
|---|---|---|
| 大脑（感知/决策） | `brain/` | Jetson Nano · ROS2 · EKF定位 · SLAM建图 · Nav2导航 · YOLO视觉 |
| 小脑（实时控制） | `cerebellum/` | RK3568 · PREEMPT-RT · IgH EtherCAT · ros2_control · 雷赛L7EC伺服 |
| 前端 | `client/` | Nuxt · Web监控 |
| 云端 | `cloud/` | 远程服务 |
| 脚本 | `scripts/` | 配网 · frp内网穿透 |

## 核心成果

- **1ms 周期实时控制**：抖动标准差 86.6μs → 6.3μs（13.7倍），最大抖动 10ms → 308μs（32.5倍）
- **共享内存零拷贝 IPC**：双缓冲 + 原子索引，打通 IgH 主站与 ros2_control 进程
- **自研控制流仲裁器 + 多边形空气墙**：替代官方 cmd_vel_mux / nav2_collision_monitor
- **EKF 融合定位**：IMU 角速度 + 轮式里程计，转弯信 IMU、直行信 Odom
- **通信三流分离**：视频(RTMP) / 话题(rosbridge+frpc) / 控制(MQTT) 独立通道
