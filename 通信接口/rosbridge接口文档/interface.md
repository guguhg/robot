## 📡 ROS2 话题接口文档（修正版）

**版本**: v1.0
**更新日期**: 2026-07-26
**rosbridge 地址**: `ws://192.168.1.30:9090`

### 📋 连接信息

| 项目     | 值                                |
| :------- | :-------------------------------- |
| 协议     | WebSocket (rosbridge v2.0)        |
| 地址     | `ws://192.168.1.30:9090`          |
| 控制话题 | `/manual_cmd_vel`（推荐，带避障） |
| 消息类型 | `geometry_msgs/msg/Twist`         |

### 🎮 控制指令（发布）

| 话题              | 消息类型                                      | 方向     | 说明                                             |
| :---------------- | :-------------------------------------------- | :------- | :----------------------------------------------- |
| `/manual_cmd_vel` | `geometry_msgs/msg/Twist`                     | **发布** | **手动控制（推荐）**，带方向感知避障，优先级最高 |
| `/cmd_vel`        | `geometry_msgs/msg/Twist`                     | **发布** | 直接控制，**无避障**，优先级较低（Nav2 使用）    |
| `/goal_pose`      | `geometry_msgs/msg/PoseStamped`               | **发布** | 设置 Nav2 导航目标点                             |
| `/initialpose`    | `geometry_msgs/msg/PoseWithCovarianceStamped` | **发布** | 初始化 AMCL 定位位姿                             |

### 📊 定位与导航（订阅）

| 话题                 | 消息类型                                      | 方向     | 说明                                       |
| :------------------- | :-------------------------------------------- | :------- | :----------------------------------------- |
| `/cmd_vel_safe`      | `geometry_msgs/msg/Twist`                     | **订阅** | **安全速度反馈**，避障修正后的实际控制指令 |
| `/odom`              | `nav_msgs/msg/Odometry`                       | **订阅** | 里程计数据（来自 brain_controller）        |
| `/odometry/filtered` | `nav_msgs/msg/Odometry`                       | **订阅** | EKF 融合后的里程计（推荐使用）             |
| `/amcl_pose`         | `geometry_msgs/msg/PoseWithCovarianceStamped` | **订阅** | AMCL 定位位姿（地图坐标系）                |
| `/map`               | `nav_msgs/msg/OccupancyGrid`                  | **订阅** | 当前地图数据                               |
| `/map_updates`       | `nav_msgs/msg/OccupancyGrid`                  | **订阅** | 地图更新增量                               |

### 📷 传感器数据（订阅）

| 话题                    | 消息类型                    | 方向     | 说明                      |
| :---------------------- | :-------------------------- | :------- | :------------------------ |
| `/aurora/rgb/image_raw` | `sensor_msgs/msg/Image`     | **订阅** | RGB 图像（320×200, bgr8） |
| `/scan`                 | `sensor_msgs/msg/LaserScan` | **订阅** | 激光雷达扫描数据          |
| `/imu/data_raw`         | `sensor_msgs/msg/Imu`       | **订阅** | IMU 原始数据              |

### 🔧 状态反馈（订阅）

| 话题                    | 消息类型                     | 方向     | 说明                     |
| :---------------------- | :--------------------------- | :------- | :----------------------- |
| `/chassis/motor_states` | `interfaces/msg/MotorStates` | **订阅** | 电机状态反馈（4 轮）     |
| `/chassis/motor_cmd`    | `interfaces/msg/MotorCmd`    | **发布** | 电机控制指令（内部使用） |
| `/joint_states`         | `sensor_msgs/msg/JointState` | **订阅** | 关节状态（用于 URDF）    |
| `/bms/soc`              | `std_msgs/msg/Float32`       | **订阅** | 电池电量（0-100）        |
| `/bms/voltage`          | `std_msgs/msg/Float32`       | **订阅** | 电池电压（V）            |

### 🗺️ 代价地图（订阅）

| 话题                              | 消息类型                           | 方向     | 说明                  |
| :-------------------------------- | :--------------------------------- | :------- | :-------------------- |
| `/global_costmap/costmap`         | `nav2_msgs/msg/Costmap`            | **订阅** | 全局代价地图          |
| `/global_costmap/costmap_raw`     | `nav_msgs/msg/OccupancyGrid`       | **订阅** | 全局代价地图原始数据  |
| `/global_costmap/costmap_updates` | `map_msgs/msg/OccupancyGridUpdate` | **订阅** | 全局代价地图更新      |
| `/local_costmap/costmap`          | `nav2_msgs/msg/Costmap`            | **订阅** | 局部代价地图          |
| `/local_costmap/costmap_raw`      | `nav_msgs/msg/OccupancyGrid`       | **订阅** | 局部代价地图原始数据  |
| `/local_costmap/voxel_grid`       | `nav2_msgs/msg/VoxelGrid`          | **订阅** | 3D 体素网格（避障用） |

### 🛡️ 调试与可视化（订阅）

| 话题                       | 消息类型                           | 方向     | 说明                    |
| :------------------------- | :--------------------------------- | :------- | :---------------------- |
| `/polygon_visualization`   | `geometry_msgs/msg/PolygonStamped` | **订阅** | 避障多边形（Rviz 调试） |
| `/particle_cloud`          | `geometry_msgs/msg/PoseArray`      | **订阅** | AMCL 粒子云             |
| `/local_plan`              | `nav_msgs/msg/Path`                | **订阅** | 局部路径规划            |
| `/plan`                    | `nav_msgs/msg/Path`                | **订阅** | 全局路径规划            |
| `/received_global_plan`    | `nav_msgs/msg/Path`                | **订阅** | Nav2 接收到的全局规划   |
| `/transformed_global_plan` | `nav_msgs/msg/Path`                | **订阅** | 转换后的全局规划        |
| `/tf`                      | `tf2_msgs/msg/TFMessage`           | **订阅** | TF 变换树（动态）       |
| `/tf_static`               | `tf2_msgs/msg/TFMessage`           | **订阅** | 静态 TF 变换            |
| `/marker`                  | `visualization_msgs/msg/Marker`    | **订阅** | Rviz 可视化标记         |
| `/cost_cloud`              | `sensor_msgs/msg/PointCloud2`      | **订阅** | 代价地图点云            |
| `/speed_limit`             | `std_msgs/msg/Float32`             | **订阅** | 速度限制值              |

### 📋 日志与系统状态（订阅）

| 话题                 | 消息类型                              | 方向     | 说明                        |
| :------------------- | :------------------------------------ | :------- | :-------------------------- |
| `/diagnostics`       | `diagnostic_msgs/msg/DiagnosticArray` | **订阅** | 系统诊断信息                |
| `/behavior_tree_log` | `nav2_msgs/msg/BehaviorTreeLog`       | **订阅** | 行为树执行日志              |
| `/robot_description` | `std_msgs/msg/String`                 | **订阅** | URDF 模型描述（XML 字符串） |
| `/rosout`            | `rcl_interfaces/msg/Log`              | **订阅** | ROS 日志输出                |

### 🔄 生命周期事件（订阅，调试用）

| 话题                                              | 消息类型                             | 方向     | 说明                            |
| :------------------------------------------------ | :----------------------------------- | :------- | :------------------------------ |
| `/amcl/transition_event`                          | `lifecycle_msgs/msg/TransitionEvent` | **订阅** | AMCL 生命周期状态变化           |
| `/bt_navigator/transition_event`                  | `lifecycle_msgs/msg/TransitionEvent` | **订阅** | BT Navigator 生命周期变化       |
| `/controller_server/transition_event`             | `lifecycle_msgs/msg/TransitionEvent` | **订阅** | Controller Server 生命周期变化  |
| `/planner_server/transition_event`                | `lifecycle_msgs/msg/TransitionEvent` | **订阅** | Planner Server 生命周期变化     |
| `/waypoint_follower/transition_event`             | `lifecycle_msgs/msg/TransitionEvent` | **订阅** | Waypoint Follower 生命周期变化  |
| `/map_server/transition_event`                    | `lifecycle_msgs/msg/TransitionEvent` | **订阅** | Map Server 生命周期变化         |
| `/controller_manager/transition_event`            | `lifecycle_msgs/msg/TransitionEvent` | **订阅** | Controller Manager 生命周期变化 |
| `/global_costmap/global_costmap/transition_event` | `lifecycle_msgs/msg/TransitionEvent` | **订阅** | 全局代价地图生命周期变化        |
| `/local_costmap/local_costmap/transition_event`   | `lifecycle_msgs/msg/TransitionEvent` | **订阅** | 局部代价地图生命周期变化        |

### 📌 其他话题

| 话题             | 消息类型                         | 方向     | 说明                |
| :--------------- | :------------------------------- | :------- | :------------------ |
| `/clicked_point` | `geometry_msgs/msg/PointStamped` | **订阅** | Rviz 中点击的地图点 |
| `/waypoints`     | `nav_msgs/msg/Path`              | **订阅** | 航点列表            |

### 🧭 导航控制（服务/动作）

| 服务/动作                              | 类型                                    | 说明               |
| :------------------------------------- | :-------------------------------------- | :----------------- |
| `/bt_navigator/navigate_to_pose`       | `nav2_msgs/action/NavigateToPose`       | 导航到目标点       |
| `/bt_navigator/navigate_through_poses` | `nav2_msgs/action/NavigateThroughPoses` | 经过多个目标点导航 |
| `/planner_server/compute_path_to_pose` | `nav2_msgs/action/ComputePathToPose`    | 计算全局路径       |
| `/controller_server/follow_path`       | `nav2_msgs/action/FollowPath`           | 跟随路径控制       |

### 📋 话题分类速查表

| 分类         | 话题                                                         |
| :----------- | :----------------------------------------------------------- |
| **控制**     | `/manual_cmd_vel`, `/cmd_vel`, `/cmd_vel_safe`               |
| **定位**     | `/amcl_pose`, `/odom`, `/odometry/filtered`, `/initialpose`  |
| **导航**     | `/goal_pose`, `/map`, `/plan`, `/local_plan`                 |
| **传感器**   | `/aurora/rgb/image_raw`, `/scan`, `/imu/data_raw`            |
| **状态**     | `/joint_states`, `/chassis/motor_states`, `/bms/soc`, `/bms/voltage` |
| **代价地图** | `/global_costmap/*`, `/local_costmap/*`                      |
| **调试**     | `/polygon_visualization`, `/particle_cloud`, `/tf`           |

### 📌 Python 脚本控制示例

```python
import asyncio
import websockets
import json

async def control_robot():
    uri = "ws://192.168.1.30:9090"
    async with websockets.connect(uri) as ws:
        # 1. 发布速度指令
        twist = {
            "op": "publish",
            "topic": "/manual_cmd_vel",
            "msg": {
                "linear": {"x": 0.2, "y": 0.0, "z": 0.0},
                "angular": {"x": 0.0, "y": 0.0, "z": 0.0}
            }
        }
        await ws.send(json.dumps(twist))
        print("✅ 已发送速度指令")

        # 2. 订阅安全速度反馈
        await ws.send(json.dumps({
            "op": "subscribe",
            "topic": "/cmd_vel_safe",
            "type": "geometry_msgs/msg/Twist"
        }))
        print("📥 已订阅 /cmd_vel_safe")

        # 3. 等待一条消息
        response = await asyncio.wait_for(ws.recv(), timeout=2.0)
        data = json.loads(response)
        print(f"📥 收到: {data}")

asyncio.run(control_robot())
```



### 📌 C# 连接示例（RosSharp）

```c#
using RosSharp.RosBridgeClient;
using RosSharp.RosBridgeClient.Messages.Geometry;

var socket = new RosSocket(new WebSocketProtocol("ws://192.168.1.30:9090"));

// 发布速度指令
var twist = new Twist
{
    linear = new Vector3 { x = 0.2, y = 0.0, z = 0.0 },
    angular = new Vector3 { x = 0.0, y = 0.0, z = 0.0 }
};
socket.Publish("/manual_cmd_vel", twist);
```



### 📌 重要说明

1. **消息类型格式**：所有消息类型采用 ROS 2 标准格式 `包名/msg/类型名`，如 `geometry_msgs/msg/Twist`。`rosbridge` 协议中 `type` 字段需要完整填写此格式。

2. **`rosbridge` 协议中的 `type` 字段**：订阅时建议添加 `type` 字段以确保正确解析：

   ```json
   {
       "op": "subscribe",
       "topic": "/cmd_vel_safe",
       "type": "geometry_msgs/msg/Twist"
   }
   ```

3. **`interfaces` 包**：`/chassis/motor_cmd` 和 `/chassis/motor_states` 使用的是自定义消息类型，定义在 `interfaces` 包中，需要在两端（大脑/小脑）编译。

4. **`lifecycle_msgs`**：生命周期事件话题主要用于调试，正常使用中可忽略。