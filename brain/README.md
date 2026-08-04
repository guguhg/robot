
## 大脑

### 负责模块

#### 描述模块

- 完整urdf文件：描述硬件之间的坐标关系以及仿真参数，base_footprint、base_link、imu、lidar、mic、camera、四个轮子

#### 驱动模块

- IMU发布/imu/data_raw 坐标系imu_link

- 激光雷达发布/scan 坐标系lidar_frame

- 摄像头发布如下，坐标系depth_camera_link_1、rgb_camera_link_1

| 话题                      | 说明                                 |
| :------------------------ | :----------------------------------- |
| /aurora/rgb/image_raw     | RGB 彩色图                           |
| /aurora/ir/image_raw      | IR 红外图                            |
| /aurora/depth/image_raw   | 深度图                               |
| /aurora/points2           | 点云                                 |
| /aurora/depth/camera_info | 深度相机标定信息                     |
| /aurora/ir/camera_info    | IR 相机标定信息                      |
| aurora/rgb/camera_info    | 相机内参、畸变系数、分辨率等标定信息 |

#### ekf模块

输入：map 全局参考坐标系、/odom（来自 ros2_control 的原始里程计）、/imu/data_raw、.......

输出：/odometry/filtered、odom->base_link坐标变换

robot_localization包，ekf扩展卡尔曼滤波，核心在于odom->base_link的坐标变换，作用是一个“智能融合器”，能把多个不完美的传感器数据，融合成一个比任何单一传感器都更准确、更平滑、更可靠的机器人运动状态估计。

ekf.yaml：高频动态转弯时更信任 IMU，稳态直行时更信任 Odom，目前使用2D模式

```
2d平面模式
/tf广播开启(odom->base_link)
发布/odometry/filtered

坐标系配置
map_frame: map
odom_frame: odom
base_link_frame: base_link
world_frame: odom

# ============================================================
# 里程计输入配置
# ============================================================
# ============ 位姿（Pose）：滤波器估计的最终最优值 ============
# x, y, z         : 三维空间中的位置坐标（单位：m）绝对姿态
# roll, pitch, yaw: 绕 X, Y, Z 轴的旋转角度（单位：rad）绝对姿态

# ============ 速度（Twist）：当前时刻的瞬时速率，用于预测下一帧位姿 ============
# vx, vy, vz      : 沿 X, Y, Z 方向的瞬时线速度（单位：m/s）瞬时速度
# vroll, vpitch, vyaw: 绕 X, Y, Z 轴的瞬时角速度（单位：rad/s）瞬时角速度

# ============ 加速度（Acceleration）：当前时刻的瞬时加速度 ============
# ax, ay, az      : 沿 X, Y, Z 方向的瞬时加速度（单位：m/s²）瞬时加速度

odom0: /odom
odom0_config: [false, false, false,	
               false, false, false,
               true, true, false,
               false, false, true,
               false, false, false]
odom0_queue_size: 10 #缓存队列
odom0_differential: false #表示接收的数据是绝对测量值，而非增量
odom0_relative: false #每次测量都是全局坐标系下的绝对速度，而非增量
odom0_pose_use_child_frame: false #使用父坐标odom，而非子坐标base_link

# ============================================================
# IMU 输入配置
# ============================================================
imu0: /imu/data_raw
#[x, y, z, roll, pitch, yaw, vx, vy, vz, vroll, vpitch, vyaw, ax, ay, az]
imu0_config: [false, false, false,
              false, false, false,  
              false, false, false,
              false, false, true,    #仅使用vyaw增量，ekf自行积分推算yaw
              false, false, false]
imu0_nodelay: false#无延迟
imu0_differential: false #必须为false，而非true，EKF 会对这个角速度再做一次差分（变成角加速度），滤波器将收到完全错误的物理量，导致姿态剧烈震荡甚至直接崩溃。
imu0_relative: false#绝对值
imu0_queue_size: 10#队列大小

# ============================================================
# 前馈预测配置
# ============================================================
use_control: false#关，时间戳不同步、打滑模型缺失

协方差矩阵，含义是越小越信任。[x, y, z, roll, pitch, yaw, vx, vy, vz, vroll, vpitch, vyaw, ax, ay, az]
```

ekf_3d.yaml：3D模式的刚体运动，暂不使用，带不动

```
输入：
1.传统轮式odom0
2.3D里程计odom_n
传感器			算法方案			推荐ROS工具
深度相机	RTAB-Map / VO	rtabmap_ros / ccny_rgbd
3D激光雷达	LOAM / ICP		loam_velodyne / rtabmap_ros icp_odometry
2D激光雷达	Range Flow		rf2o_laser_odometry
2D激光雷达	PL-ICP			laser_scan_matcher
3.imu0
4.imu_n
用于无人机、水下机器人
jetson nano带不动如此庞大的计算量，暂时关，不做使用。
```

局部ekf与全局ekf：未实现，仅作了解

```
一种推荐的工程实践是采用双EKF架构，将“连续定位”和“全局定位”分开处理。

本地EKF (Local EKF):
只融合连续的传感器数据，如2D ekf/ 3D ekf
其world_frame设置为odom。
这个滤波器负责提供高频、平滑的局部轨迹。

全局EKF (Global EKF):
1.robot_localization 包 navsat_transform_node节点进行翻译为odom
/odometry/local、/gps/fix、/imu/data->navsat_transform_node->/odometry/navsat
2.ekf
/odometry/navsat、/odometry/local->全局ekf->/odometry/filtered
其world_frame设置为map。
这个滤波器负责提供全局无漂移的定位。

注意：如果系统不复杂，也可以只用一个EKF节点
```

#### slam模块

SLAM Toolbox，一个2Dslam的常用方案

输入/scan话题、odom->base_link坐标变换（EKF 的优化里程计）

输出/map话题、map->odom坐标变换

slam_params.yaml

```
# ============================================================
# ROS 坐标系配置
# ============================================================
# 里程计坐标系（机器人短期定位基准，会漂移）
odom_frame: odom	#这里的odom坐标系是由ekf发布，解决了轮式odom的建图漂移问题
# 地图坐标系（全局固定，用于建图和长期定位）
map_frame: map
# 机器人本体坐标系（必须与 URDF 中的 base_frame 一致）
base_frame: base_link

# ============================================================
# 话题与模式配置
# ============================================================
# 激光雷达扫描数据话题
scan_topic: /scan
# 是否启用地图保存功能
use_map_saver: true
# 运行模式：mapping（建图）或 localization（定位）
mode: mapping  # 可选: localization
```

#### nav2模块

导航算法的标准实现，map → odom（AMCL/SLAM 提供）→ base_link（EKF 提供） → lidar_frame（URDF 提供）

nav2_params.yaml，通用nav2配置，2D避障

```
AMCL 自适应蒙特卡洛定位定位模块，通过激光雷达和地图匹配，估计机器人在 map 中的位姿
bt_navigator 行为树导航器，进行在某种情况应该怎么做的规则定义
controller_server 控制器服务器（局部路径跟踪），根据全局路径，实时计算速度指令控制机器人移动
local_costmap 局部代价地图，机器人周围动态障碍物地图，用于实时避障
global_costmap 全局代价地图，基于地图的全局障碍物地图，用于全局路径规划
map_server 地图服务器，加载并发布保存的地图
planner_server 规划器服务器（全局路径规划），根据地图和当前位置，规划从起点到目标点的全局路径
smoother_server 平滑器服务器，对全局路径进行平滑处理，使运动更流畅
behavior_server 行为服务器，当机器人卡住或无法前进时，执行恢复行为
waypoint_follower 航点跟随器，按顺序执行多个导航目标点
velocity_smoother 速度平滑器，平滑速度指令，防止急停急转
```

nav2_params_pointcloud.yaml，多观测点nav2配置，用于3D避障，已实现，加入3D点云太卡了。

```
local_costmap
 observation_sources: scan pointcloud   #观测点只支持点云，使用2D激光雷达+深度相机3D点云
        scan:
          topic: /scan
          max_obstacle_height: 2.0
          clearing: True
          marking: True
          data_type: "LaserScan"
          raytrace_max_range: 3.0
          raytrace_min_range: 0.0
          obstacle_max_range: 2.5
          obstacle_min_range: 0.0
        pointcloud:
          topic: /aurora/points2
          max_obstacle_height: 1.5
          clearing: True
          marking: True
          data_type: "PointCloud2"
          obstacle_max_range: 2.5
          obstacle_min_range: 0.0
```

| 模块                  | 输入话题                | 输出话题                        | 输出 TF            |
| :-------------------- | :---------------------- | :------------------------------ | :----------------- |
| **map_server**        | 地图文件                | `/map`                          | —                  |
| **AMCL**              | `/scan`, `/map`, `/tf`  | `/amcl_pose`, `/particle_cloud` | **`map → odom`** ✅ |
| **planner_server**    | `/map`, `/tf`           | `/plan`                         | —                  |
| **controller_server** | `/plan`, `/tf`, `/scan` | `/cmd_vel`, `/local_plan`       | —                  |
| **bt_navigator**      | `/tf`                   | `/bt_navigator/status`          | —                  |
| **behavior_server**   | `/tf`, 代价地图         | `/cmd_vel`                      | —                  |
| **waypoint_follower** | —                       | 调用导航动作                    | —                  |

#### 视觉模块

```
rgb：用于yolo识别 、视觉巡线、人机交互、原画监控
ir：弱光环境感知、夜间目标检测 
depth：nav2 代价地图，增强避障 、距离测量 
point：3D 建图、点云避障  
rgb+depth：用于ekf融合视觉里程计增强定位
```

points2 Nav2 代价地图（增强避障）：已实现，见上方

YOLO 目标检测：输入/aurora/rgb/image_raw，加载模型yolov8n.onnx与cuda，输出/aurora/rgb/image_yolo，这部分太卡了不能综合使用。

#### 控制流仲裁器模块

为什么不用cmd_vel_mux，或者twist_mux？因为一直找不到包，再加上比较简单就自己实现了一个易用版本的。

可自定义配置话题、优先级、是否开启防撞。很明显这是个结构体列表，对应话题名有优先级与防撞开关配置。

[

{"/manual_cmd_vel", 100, true},

{"/cmd_vel", 50, false}

]

初始化：每个速度源创建一个订阅，使用lambda表达式作为回调

```c++
for (const auto& source : cmd_vel_sources_) {
    auto sub = create_subscription<geometry_msgs::msg::Twist>(
      source.topic, 10,
      [this, source](const geometry_msgs::msg::Twist::SharedPtr msg) {
        cmdVelCallback(msg, source.topic, source.collision_enabled, source.priority);
      });
    cmd_vel_subs_.push_back(sub);
  }
```

回调实现：每个到来的速度指令，进行检查优先级并更新到唯一活动指令中

```c++
void SimpleCollisionMonitor::cmdVelCallback(
    const geometry_msgs::msg::Twist::SharedPtr msg,
    const std::string& topic,
    bool collision_enabled,
    int priority) {
  
  // 检查当前是否有更高优先级的有效指令
  if (active_cmd_.valid && active_cmd_.priority > priority) {
    return;  // 忽略较低优先级的指令
  }

  // 检查当前指令是否超时
  bool current_expired = false;
  if (active_cmd_.valid) {
    current_expired = (now() - active_cmd_.timestamp).seconds() > cmd_timeout_;
  }

  // 更新激活指令（条件：无有效指令 || 超时 || 更高优先级）
  if (!active_cmd_.valid || current_expired || priority > active_cmd_.priority) {
    active_cmd_.cmd = *msg;
    active_cmd_.source_topic = topic;
    active_cmd_.collision_enabled = collision_enabled;
    active_cmd_.priority = priority;
    active_cmd_.timestamp = now();
    active_cmd_.valid = true;
  }
}
```

#### base_link空气墙模块 

为什么不用nav2_collision_monitor包的collision_monitor？主要原因是因为效果没有达到预期，和我预想的相差太大。

把小车放到空旷的地方，然后运行我发现有两个问题
1.小车按一下向前，向墙壁前进，collision_monitor没有给我刹车或减速
2.如果小车在墙壁手动停下来时，按键任何操作都会输出0，向后退都没有效果

根据原理自己实现collision_avoidance,核心原理是配置四个方向的多边形，雷达点云如果有落在多边形，该方向对应操作就执行对应动作

动作类型："direction"限制运动方向, "stop"刹车禁止操作, "slowdown"减速, "limit_angular"限制角速度，目前仅实现了限制运动方向也是最常用的。（初始方案只有一个多边形，设置了减速区和停止区，顾名思义，减速和刹车的逻辑很好，表现力也是非常有冲击力，但是发现无法进行恢复行为，初始方案和现有方案逻辑上产生了逻辑冲突，因此重构为当前方案）

输入控制流仲裁器模块输出的活动指令/cmd_vel

输出/cmd_vel_safe

collision_avoidance.yaml

```
simple_collision_monitor:
  ros__parameters:
	# 机器人本体坐标系（TF 树的根坐标）
    # 所有多边形点都在此坐标系下定义
    # 坐标系：x 正方向为前，y 正方向为左
    base_frame: "base_link"

    # 激光雷达扫描数据话题
    scan_topic: "/scan"

    # 修正后的安全速度输出话题
    # brain_controller 订阅此话题执行运动
    cmd_vel_out_topic: "/cmd_vel_safe"

    # 指令超时时间（秒）
    # 超过此时间未收到新指令，自动发布零速度
    cmd_timeout: 1.0

    # 多边形可视化发布话题
    # 在 Rviz 中添加此话题（PolygonStamped）可查看多边形区域
    polygon_pub_topic: "polygon_visualization"

    # 是否发布多边形可视化
    # true=发布（调试用），false=不发布（生产环境可关闭）
    visualize_polygons: true

    # ============================================================
    # 方向感知多边形
    # ============================================================
    #
    # 工作原理：
    #   1. 将激光雷达点云转换到 base_frame 坐标系
    #   2. 统计落入每个多边形内的点数
    #   3. 若点数 >= min_points，触发该方向限制
    #   4. 同时触发多个方向时，叠加限制（各方向独立）
    #
    # 点顺序（逆时针）：前左 → 前右 → 后右 → 后左
    # 示例：points: [P1.x, P1.y, P2.x, P2.y, P3.x, P3.y, P4.x, P4.y]
    # ============================================================

    # 多边形名称列表（必须与下面的配置一一对应）
    polygon_names:
      - "Front"   # 前方
      - "Back"    # 后方
      - "Left"    # 左方
      - "Right"   # 右方

    # ============================================================
    # 多边形详细配置
    # ============================================================

    polygons:

      # ---------- 前方区域 ----------
      # 检测到障碍物时限制前进（linear.x 正方向清零）
      Front:
        # 多边形顶点（单位：米）
        # 矩形范围：x: 0.035~0.28, y: -0.21~0.21
        points:
          [0.28, 0.21,     # P1: 前左角 (x: 0.28, y: 0.21)
           0.28, -0.21,    # P2: 前右角 (x: 0.28, y: -0.21)
           0.035, -0.21,   # P3: 后右角 (x: 0.035, y: -0.21) 靠近车身
           0.035, 0.21]    # P4: 后左角 (x: 0.035, y: 0.21)
        # 动作类型：direction 表示方向限制
        action: "direction"
        # 触发阈值：至少需要多少个激光点落入多边形
        # 建议 3~5，过滤噪声，真实障碍物通常会有多个点
        min_points: 4
        # 限制的方向：
        #   forward   → 限制前进
        #   backward  → 限制后退
        #   left      → 限制左转和左移
        #   right     → 限制右转和右移
        direction: "forward"

      # ---------- 后方区域 ----------
      # 检测到障碍物时限制后退（linear.x 负方向清零）+ 禁止旋转
      Back:
        # 矩形范围：x: -0.28~-0.035, y: -0.21~0.21
        points:
          [-0.28, 0.21,    # P1: 前左角 (x: -0.28, y: 0.21)
           -0.28, -0.21,   # P2: 前右角 (x: -0.28, y: -0.21)
           -0.035, -0.21,  # P3: 后右角 (x: -0.035, y: -0.21) 靠近车身
           -0.035, 0.21]   # P4: 后左角 (x: -0.035, y: 0.21)
        action: "direction"
        min_points: 4
        direction: "backward"

      # ---------- 左侧区域 ----------
      # 检测到障碍物时限制左转（angular.z 正方向清零）+ 左移（linear.y 正方向清零）
      Left:
        # 矩形范围：x: -0.14~0.14, y: 0.035~0.28
        points:
          [0.14, 0.28,     # P1: 前左角 (x: 0.14, y: 0.28)
           0.14, 0.035,    # P2: 前右角 (x: 0.14, y: 0.035)
           -0.14, 0.035,   # P3: 后右角 (x: -0.14, y: 0.035) 靠近车身
           -0.14, 0.28]    # P4: 后左角 (x: -0.14, y: 0.28)
        action: "direction"
        min_points: 4
        direction: "left"

      # ---------- 右侧区域 ----------
      # 检测到障碍物时限制右转（angular.z 负方向清零）+ 右移（linear.y 负方向清零）
      Right:
        # 矩形范围：x: -0.14~0.14, y: -0.28~-0.035
        points:
          [0.14, -0.035,   # P1: 前左角 (x: 0.14, y: -0.035)
           0.14, -0.28,    # P2: 前右角 (x: 0.14, y: -0.28)
           -0.14, -0.28,   # P3: 后右角 (x: -0.14, y: -0.28) 靠近车身
           -0.14, -0.035]  # P4: 后左角 (x: -0.14, y: -0.035)
        action: "direction"
        min_points: 4
        direction: "right"
```

初始化阶段：加载参数、加载多边形、订阅/scan话题、监听tf、发布/polygon_visualization、发布/cmd_vel_safe

/scan回调：

```
1. 检查是否有有效的速度指令
2. 如果该指令禁用了避障，直接透传
3. 可视化多边形（统一发布到 /polygon_visualization）
4. 获取 TF 变换（lidar_frame → base_link）
5. 转换激光点到 base_frame
6. 检测多边形，判断是否有多个点云在某个多边形内部，若有将动作提取压入vector中
7. 容器为空直接跳过修正。容器有动作，对当前速度进行对应动作修正。
8. 发布/cmd_vel_safe
```

#### 系统服务模块

存放位置**bringup/scripts**，进行一些系统服务的脚本功能

- imu_calibrator.py：IMU零漂校准服务

  ```
  采样200个静止时的数据点计算xyz三轴零点漂移值，填入配置文件由驱动模块减去零漂以获取更高精度的IMU角速度。
  ```

- wifi_manager.py（宿主机）：配网服务，开机自启动，主要使用python执行nmcli命令行+Flask Web 界面服务

  ```
  开机等待10秒 → 检测Wi-Fi连接 → 未连接则开启AP → 网页配网 → 连接成功后自动保存，方便下次自动连接 → 连接失败回到第三步
  ```

- ros_to_rtmp.py：用于ros2图像消息通过cv_bridge转换为bgr8字节流，将数据写入 FFmpeg 管道进行RTMP推流

  ```
  登录获取JWT->用JWT换取推流 Token->开启Token刷新循环（当Token过期时重启FFmpeg）->启动 FFmpeg 推流到rtmp://124.222.135.234:1935/live/demo-01?token={xxx}
  图像回调：ros2图像消息通过cv_bridge转换为bgr8字节流，将数据写入 FFmpeg 管道。
  ```

- setup_frpc.sh（宿主机）：用于内网穿透服务安装脚本
  frps（服务端）：部署在有公网 IP 的服务器上（比如云服务器 124.222.135.234）。
  frpc（客户端）：部署在内网机器上（比如你的机器人）。
  frpc 就像你机器人的一个“通信兵”，它会主动连接云服务器上的 frps，并告诉它：“我这边有一个服务在 9090 端口，如果有人来找你，请转给我”。

- uninstall_frpc.sh（宿主机）：用于frp服务卸载脚本

- rosbridge.launch.py：基于WebSocket的ROS2消息桥接服务，使得外部网络可以发现机器的ROS2话题并进行通信，端口9090

#### 通信模块

- 基于WebSocket的rosbridge方案：

  ros与非ros双方使用WebSocket+JSON通信，上层可以非常方便的对话题进行通信，目前已经实现图像、控制、点云等数据的传输显示，rosbridge.launch.py一键运行即可。实际效果：2D点云、控制没有问题，但是视频延迟很高。

  ```
  接口方案位于Z:\E_data\Smart Inspection Robot\通信接口\rosbridge接口文档\interface.md
  ```

- MQTT+WebRTC+WebSocket服务器中心方案：

  视频流：ROS2 Topic-ffmpeg rtmp推流-SRS服务器 WebRTC-前端

  话题流：设备WebSocket rosbridge - frpc内网穿透 - frps服务 Nginx代理 -  前端（一开始只是为了点云流）

  控制流：前端 - .NET - EMQX - MQTT设备 / 话题流

  ```
  接口方案位于Z:\E_data\Smart Inspection Robot\通信接口\MQTT+WebRTC+WebSocket服务器中心方案\机器人端MQTT+Websocket+RTMP.md
  ```

  分流通路方案，各走各的路线，延迟都低，非常流程。

  **图像传输测试**

  ![30dbf019b2a7ac43cb8887ba08120dc0_720](项目总结.assets/30dbf019b2a7ac43cb8887ba08120dc0_720.jpg)

  ![f62199b016ddbada2da6bac3a1c122df_720](项目总结.assets/f62199b016ddbada2da6bac3a1c122df_720.jpg)

  **点云传输测试**

  ![eb676634c433b054d815e7e7943d07e1](项目总结.assets/eb676634c433b054d815e7e7943d07e1-1784945033886.png)

  **控制指令传输测试**

  ![ffd79985edb4d824a7b56fc06bacf02a](项目总结.assets/ffd79985edb4d824a7b56fc06bacf02a.png)

  **综合调试界面**

  手动控制

  ![image-20260725101451970](项目总结.assets/image-20260725101451970.png)

  导航控制

  ![image-20260725102128378](项目总结.assets/image-20260725102128378.png)

  导航+手动介入->优先级仲裁->导航恢复控制

  ![image-20260725101642469](项目总结.assets/image-20260725101642469.png)

  

### 功能项

#### bringup/launch启动项

- ros2 launch bringup a_full.launch.py map:=/ros2_ws/src/map/map_manual.yaml

  全功能启动项（地图导航）（无调试）

- ros2 launch bringup brain_full_debug_rosbridge.launch.py map:=/ros2_ws/src/map/map_manual.yaml

  全功能启动项（地图导航）

- brain_mapping.launch.py ：建图启动项

- a_localization.launch.py地图导航启动项（带控制路由与空气墙模块）（无RVIZ调试界面）

- teleop_collision_avoidance.launch.py：手动控制启动项（带空气墙模块）

- aurora_include.launch.py ：深度相机启动项                         

- brain_localization_pointcloud.launch.py ：地图导航启动项（带深度相机3D点云避障）

- image_compressor.launch.py：图像压缩启动项                 

- rosbridge.launch.py：ros桥接通信启动项

- brain_localization_collision_avoidance.launch.py：地图导航启动项（带控制路由与空气墙模块）  

- brain_slam_nav.launch.py ：边建图边导航启动项                

- brain_localization.launch.py ：地图导航启动项                     

- brain_slam_nav_pointcloud.launch.py：边建图边导航启动项 （带深度相机3D点云避障）   

- teleop_collision_monitor.launch.py：手动控制启动项（官方nav2_collision_monitor模块，效果比较差）

#### description/config配置项

- collision_avoidance.yaml：仲裁器与空气墙模块配置
- ekf_3d.yaml：3D ekf模块配置，实验性
- nav2_params_pointcloud.yaml：nav2导航模块配置，带激光雷达2D点云+深度相机3D点云观测点
- slam_params.yaml：slam模块配置
- collision_monitor.yaml ：官方nav2_collision_monitor模块配置，实验性
- ekf.yaml ：2D ekf模块配置
- nav2_params.yaml：nav2导航模块配置，带激光雷达2D点云
- behavior_trees/navigate_to_pose_without_backup.xml：每秒刷新一次路线、走不动就清地图、清完还不行就原地转圈/等待，且死活不倒车的稳健自动导航策略。 
- behavior_trees/navigate_without_backup.xml：一个“走点巡航”专用的行为树——每隔 3 秒刷新一次路线，边走边自动划掉已走过的点，卡住了就转圈/等待，但绝不倒车。适合场景：园区无人配送、仓库货架巡检、商场清洁等低速、固定路线、多停靠点的任务。

#### description/rviz配置项

- view_collisionMonitor.rviz：综合调试界面配置项（带nav2_collision_monitor墙体），实验性
- view_pointcloud.rviz：综合调试界面配置项（带3D点云）
- view.rviz 综合调试界面配置项（带空气墙）

#### description/urdf配置项

与描述模块是同一个东西

#### map地图项

保存地图的地方

### 数据流

#### TF树

![frames_2026-07-22_10.05.21](项目总结.assets/frames_2026-07-22_10.05.21.jpg)

map → odom（AMCL/SLAM 提供）→ base_link（EKF 提供） → lidar_frame（URDF 提供）

map → odom（AMCL/SLAM 提供）

odom→ base_link（EKF 提供）

base_link → lidar_frame/xxx（URDF 提供）

#### 话题

```
┌─────────────────────────────────────────────────────────────────────────────────┐
│                              大脑                                                │
├─────────────────────────────────────────────────────────────────────────────────┤
│                                                                                 │
│  ┌──────────────┐      ┌──────────────┐      ┌──────────────┐                 │
│  │  IMU 驱动    │      │  激光雷达驱动 │      │  相机驱动    │                 │
│  │              │      │  (oradar)    │      │  (Aurora)    │                 │
│  └──────┬───────┘      └──────┬───────┘      └──────┬───────┘                 │
│         │                     │                     │                          │
│         ▼                     ▼                     ▼                          │
│  /imu/data_raw        	/scan              /aurora/rgb/image_raw             │
│                                              /aurora/ir/image_raw              │
│                                              /aurora/depth/image_raw           │
│                                              /aurora/points2                   │
│											   /aurora/rgb/camera_info			 │
│											   /aurora/depth/camera_info         │
│											   /aurora/ir/camera_info            │
│         │                     │                     │                          │
│         └──────────┬──────────┴──────────┬──────────┘                          │
│                    ▼                    ▼                                      │
│           ┌──────────────────────────────────────────────────────┐             │
│           │              robot_state_publisher                   │             │
│           │        (发布 TF 树: /tf, /tf_static)                  │             │
│           └──────────────────────────────────────────────────────┘             │
│                    │                    │                                      │
│                    ▼                    ▼                                      │
│           ┌──────────────────────────────────────────────────────┐             │
│           │            ekf_filter_node (robot_localization)      │             │
│           │    输入: /imu/data_raw, /odom                         │             │
│           │    输出: /odometry/filtered                           │             │
│           └──────────────────────────────────────────────────────┘             │
│                    │                    │                                      │
│                    ▼                    ▼                                      │
│  ┌─────────────────────────────────────────────────────────────────────────┐   │
│  │            建图模式SLAM or 导航模式AMCL (定位) + Nav2 (导航)                │   │
│  │  输入: /scan, /map, /odometry/filtered, /goal_pose                        │  │
│  │  输出: /amcl_pose, /plan, /cmd_vel (→ /nav_cmd_vel)                       │  │
│  └─────────────────────────────────────────────────────────────────────────┘  │
│                    │                                                           │
│                    ▼                                                           │
│           ┌──────────────────────────────────────────────────────┐             │
│           │         cmd_vel_mux (优先级仲裁)                       │             │
│           │  输入: /nav_cmd_vel (Nav2)                            │             │
│           │        /teleop_cmd_vel (键盘)                         │             │
│           │        /manual_cmd_vel (外部控制)                      │             │
│           │  输出: /cmd_vel_muxed                                  │             │
│           └──────────────────────────────────────────────────────┘             │
│                    │                                                           │
│                    ▼                                                           │
│           ┌──────────────────────────────────────────────────────┐             │
│           │    simple_collision_monitor (空气墙)                  │             │
│           │  输入: /cmd_vel_muxed, /scan                         │             │
│           │  输出: /cmd_vel_safe, /polygon_visualization         │             │
│           └──────────────────────────────────────────────────────┘             │
│                    │                                                           │
│                    ▼                                                           │
┌─────────────────────────────────────────────────────────────────────────────────┐
│                              小脑                                                │
├─────────────────────────────────────────────────────────────────────────────────┤
│           ┌──────────────────────────────────────────────────────┐             │
│           │         小脑controller                                │             │
│           │  输入: /cmd_vel_safe，/odometry/filtered              │             │
│           └──────────────────────────────────────────────────────┘             │
│                    │                                                           │
│                    ▼                                                           │
│           attitude_compensator->IK->write->电机                                  │
│                                                                                 │
│  ┌─────────────────────────────────────────────────────────────────────────┐  │
│  │    [rosbridge_websocket-> frpc] ->frps -> Nginx代理 -> Web          		 │  │
│  │  端口 9090，供外部 Web/客户端 订阅/发布                               		 │  │
│  └─────────────────────────────────────────────────────────────────────────┘  │
│           ┌──────────────────────────────────────────────────────┐             │
│			│			ros_to_rtmp (RTMP 推流)					  │				│
│           │ [image_raw->ffmpeg->rtmp推流]->SRS服务器->WebRTC->Web  │             │             
│           │  输入: /aurora/rgb/image_raw                          │             │
│           │  输出: rtmp://服务器/live/demo-01                      │             │
│           └──────────────────────────────────────────────────────┘             │
└─────────────────────────────────────────────────────────────────────────────────┘
```
