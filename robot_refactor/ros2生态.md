## ROS 2 完整组件体系

### 一、核心基础层

这部分是 ROS 2 的“地基”，任何 ROS 2 系统都离不开它们。

#### 1.1 客户端库与运行时

| 组件                    | 说明                                  |
| :---------------------- | :------------------------------------ |
| `rclcpp`                | C++ 客户端库，所有 C++ 节点的基础     |
| `rclpy`                 | Python 客户端库                       |
| `rcl`                   | 纯 C 核心库，被 `rclcpp`/`rclpy` 封装 |
| `rcl_action`            | 动作（Action）通信支持                |
| `rcl_lifecycle`         | 生命周期节点支持                      |
| `rcutils` / `rcpputils` | 通用工具函数                          |

#### 1.2 中间件与通信

| 组件                 | 说明                          |
| :------------------- | :---------------------------- |
| `rmw`                | ROS 中间件接口，抽象 DDS 差异 |
| `rmw_fastrtps_cpp`   | Fast DDS 实现                 |
| `rmw_cyclonedds_cpp` | Eclipse Cyclone DDS 实现      |
| `rosidl_*`           | 消息/服务/动作的代码生成器    |

#### 1.3 接口定义（核心消息）

| 组件                 | 内容                                       |
| :------------------- | :----------------------------------------- |
| `std_msgs`           | 基础消息类型（String, Header, Float32 等） |
| `builtin_interfaces` | 内置类型（Time, Duration）                 |
| `sensor_msgs`        | 传感器消息（Imu, LaserScan, Image 等）     |
| `geometry_msgs`      | 几何消息（Twist, Pose, Quaternion 等）     |
| `std_srvs`           | 标准服务（Empty, Trigger 等）              |
| `action_msgs`        | 动作通信消息                               |

#### 1.4 构建与工具

| 组件                           | 说明                                     |
| :----------------------------- | :--------------------------------------- |
| `ament_cmake` / `ament_python` | 构建系统                                 |
| `colcon`                       | 命令行构建工具                           |
| `ros2cli`                      | 命令行工具集（ros2 run, topic, node...） |
| `launch` / `launch_ros`        | 启动系统                                 |
| `rosdep`                       | 依赖管理工具                             |
| `class_loader`                 | 动态加载插件（如 ros2_control 硬件插件） |

------

### 二、坐标变换与机器人描述

这部分是 ROS 2 的“空间感”基础。

#### 2.1 tf2 坐标变换栈

| 组件                | 说明                                               |
| :------------------ | :------------------------------------------------- |
| `tf2`               | 坐标变换核心库                                     |
| `tf2_ros`           | ROS 绑定（TransformBroadcaster, Buffer, Listener） |
| `tf2_geometry_msgs` | geometry_msgs 类型变换支持                         |
| `tf2_sensor_msgs`   | sensor_msgs 类型变换支持                           |
| `tf2_tools`         | 调试工具（tf2_echo, view_frames）                  |
| `tf2_kdl`           | KDL 集成                                           |
| `tf2_eigen`         | Eigen 集成                                         |

#### 2.2 URDF 机器人模型

| 组件                    | 说明                       |
| :---------------------- | :------------------------- |
| `urdf`                  | URDF 解析器                |
| `urdf_parser_plugin`    | URDF 插件接口              |
| `xacro`                 | URDF 宏语言，支持条件/变量 |
| `kdl_parser`            | KDL 解析器                 |
| `robot_state_publisher` | 发布 robot_state 到 tf2    |
| `joint_state_publisher` | 发布关节状态               |

------

### 三、控制框架

这部分是 `ros2_control` 生态，你的小脑核心。

#### 3.1 ros2_control 核心

| 组件                      | 说明                                             |
| :------------------------ | :----------------------------------------------- |
| `hardware_interface`      | 硬件抽象接口（SystemInterface, SensorInterface） |
| `controller_interface`    | 控制器抽象接口                                   |
| `controller_manager`      | 控制器管理器，加载/卸载控制器                    |
| `transmission_interface`  | 传动机构抽象（减速器）                           |
| `controller_manager_msgs` | 控制器管理器消息                                 |
| `realtime_tools`          | 实时工具（RealtimeBuffer, RealtimePublisher）    |
| `control_toolbox`         | 控制工具（PID 滤波器等）                         |

#### 3.2 ros2_controllers（官方控制器）

| 控制器                            | 说明             |
| :-------------------------------- | :--------------- |
| `mecanum_drive_controller`        | 麦轮底盘控制器   |
| `diff_drive_controller`           | 差速底盘控制器   |
| `ackermann_steering_controller`   | 阿克曼转向控制器 |
| `bicycle_steering_controller`     | 自行车模型控制器 |
| `tricycle_controller`             | 三轮车控制器     |
| `joint_trajectory_controller`     | 关节轨迹控制器   |
| `joint_state_broadcaster`         | 关节状态广播器   |
| `imu_sensor_broadcaster`          | IMU 传感器广播器 |
| `force_torque_sensor_broadcaster` | 力/力矩传感器    |
| `forward_command_controller`      | 前向指令控制器   |
| `pid_controller`                  | PID 控制器       |
| `velocity_controllers`            | 速度控制器组     |
| `effort_controllers`              | 力矩控制器组     |
| `position_controllers`            | 位置控制器组     |

------

### 四、感知与算法

这部分是“看懂世界”的能力。

#### 4.1 传感器数据处理

| 组件                          | 说明                  | 使用情况       |
| :---------------------------- | :-------------------- | :------------- |
| `image_transport`             | 图像压缩传输          | 有摄像头时用   |
| `cv_bridge`                   | OpenCV ↔ ROS 图像桥接 | 视觉用         |
| `pcl_ros` / `pcl_conversions` | PCL 点云处理          | 3D 激光雷达用  |
| `filters`                     | 通用滤波器库          | 可能用到       |
| `message_filters`             | 时间同步的消息滤波    | 多传感器融合用 |

#### 4.2 SLAM 与建图

| 组件             | 说明                             |
| :--------------- | :------------------------------- |
| `slam_toolbox`   | 2D 激光 SLAM 建图                |
| `cartographer`   | Google 的 2D/3D SLAM（非官方包） |
| `orb_slam3_ros2` | 视觉 SLAM                        |

#### 4.3 导航

| 组件                     | 说明                 |
| :----------------------- | :------------------- |
| `nav2_bringup`           | Nav2 启动文件集      |
| `nav2_planner`           | 全局路径规划器       |
| `nav2_controller`        | 局部路径跟踪器       |
| `nav2_amcl`              | 概率定位（蒙特卡洛） |
| `nav2_map_server`        | 地图加载/保存        |
| `nav2_lifecycle_manager` | 生命周期节点管理     |

#### 4.4 状态估计与融合

| 组件                  | 说明                                         |
| :-------------------- | :------------------------------------------- |
| `robot_localization`  | EKF/UKF 多传感器融合                         |
| `imu_tools`           | IMU 数据处理工具（`imu_filter_madgwick` 等） |
| `imu_filter_madgwick` | Madgwick 姿态估计算法                        |
| `imu_filter_mahony`   | Mahony 互补滤波算法                          |

------

### 五、可视化与调试

| 组件                              | 说明                                           |
| :-------------------------------- | :--------------------------------------------- |
| `rviz2`                           | 3D 可视化工具                                  |
| `rqt`                             | GUI 工具集（rqt_graph, rqt_plot, rqt_console） |
| `rqt_controller_manager`          | 控制器管理 GUI                                 |
| `rqt_joint_trajectory_controller` | 关节轨迹控制 GUI                               |
| `ros2bag`                         | 数据录制/回放                                  |
| `ros2doctor`                      | 系统健康检查                                   |
| `ros1_bridge`                     | ROS 1 ↔ ROS 2 桥接                             |

------

### 六、高级功能包

| 组件               | 说明           | 使用情况     |
| :----------------- | :------------- | :----------- |
| `MoveIt2`          | 机械臂运动规划 | 有机械臂时用 |
| `BehaviorTree.CPP` | 行为树框架     | 任务编排用   |
| `SMACC2`           | 状态机框架     | 任务编排用   |
| `PlanSys2`         | PDDL 规划器    | 任务编排用   |