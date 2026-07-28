# 项目架构重构

| 模块         | 定义                                                     | 核心职责                                                     | 依赖关系                     |
| :----------- | :------------------------------------------------------- | :----------------------------------------------------------- | :--------------------------- |
| **驱动**     | 非 ROS 2 模块，直接与硬件对话，并提供移植扩展接口        | 封装硬件通信协议（串口、CAN），提供统一的硬件操作接口（如 `MOTOR_CTRL` 宏） | 可独立运行，不依赖 ROS 2     |
| **驱动接口** | 驱动与 ROS 世界的桥梁，负责读取硬件数据并发布到 ROS 话题 | 将硬件数据转换成标准 ROS 消息（如 `sensor_msgs/Imu`），并接收组件控制命令 | 依赖驱动模块                 |
| **公共**     | 非 ROS 2 的跨模块工具集                                  | 提供配置文件加载、日志记录等基础功能                         | 被所有需要基础功能的模块依赖 |
| **系统服务** | 面向开发/维护的独立工具和外部通信接口                    | 提供 IMU 校准、诊断脚本、AP配置、gRPC API 网关等，用于系统配置和二次开发 | 依赖公共和接口模块           |
| **接口**     | 模块间通信的“契约”                                       | 定义 ROS 话题（`.msg`）、服务（`.srv`）和动作（`.action`）的类型 | 被需要通信的模块依赖         |
| **启动**     | 系统的“总装车间”                                         | 通过 `.launch.py` 文件，将**组件**和**驱动接口**按正确顺序和配置组合起来，启动系统 | 依赖所有模块                 |
| **组件**     | **ROS 2 功能包**                                         | 包括 `ros2_control`, `nav2`, `slam_toolbox` 等。你可以直接使用、组合这些官方组件，或自己新建。这正是 ROS 2 强大之处 | 依赖接口和公共模块           |
| **任务编排** | 机器人的“大脑”核心，负责复杂行为决策                     | 使用行为树或状态机，调用系统服务或组件提供的动作接口，实现从“到 A 点”到“执行任务”的高级逻辑 | 依赖系统服务和组件模块       |

# 小脑重构

```
sudo docker start -ai cerebellum
cd /ros2_ws
colcon build --symlink-install 
source install/setup.bash
ros2 launch bringup dri_bringup.launch.py
```

驱动模块：

```
无需重构
```

驱动接口模块：

```
IMU模块测试
calib_mode: true
publish_relative: true
# 终端1: 启动 imu_tools_node（不取逆）
ros2 launch bringup dri_bringup.launch.py
ros2 run algorithms imu_tools_node

# 终端2: 发布静态 TF（绕 X 轴 180°）
ros2 run tf2_ros static_transform_publisher 0 0 0 0 0 3.14159 imu_link_fixed imu_link

# 终端3: 用 imu_transformer 变换
ros2 run imu_transformer imu_transformer_node \
  --ros-args \
  -r imu_in:=/imu/data \
  -r imu_out:=/imu/data_corrected \
  -p target_frame:=imu_link_fixed

# 终端4: 查看修正后的四元数
ros2 topic echo /imu/data_corrected --field orientation
```

```
# 1. 启动 imu_dri
ros2 launch bringup dri_bringup.launch.py

# 2. 启动 imu_tools_node
ros2 run algorithms imu_tools_node

# 3. 运行 TF 校准工具
ros2 run system_services tf_fixer_calibrator

# 4. 按提示做 4 个动作
#    - 静止
#    - 左转
#    - 右转
#    - 左倾
#    - 右倾

# 5. 生成 tf_fixer.yaml
rotation:
  pitch: 0.0
  roll: 3.14159265
  yaw: 0.0
  
# 6. 启动算法层 (自动读取 tf_fixer.yaml)
ros2 launch bringup imu_bringup.launch.py

# 7. 验证
ros2 topic echo /imu/data_corrected --field orientation

description: TF 修正参数 (由 tf_fixer_calibrator 生成)
enabled: true
generated_by: system_services/tf_fixer_calibrator.py
input_topic: /imu/data
output_topic: /imu/data_corrected
rotation:
  pitch: 0.0
  roll: 0.0
  yaw: 3.14159265
source_frame: imu_link
target_frame: imu_link_fixed

静止时
---
x: -0.9999902248378717
y: -0.0033946633287505815
z: -0.002864923334254309
w: -0.00021458224136067423
---
倒立时
---
x: -0.3879612979602922
y: 0.46607062080372214
z: 0.781110287549195
w: -0.14876508782403594
---
抬头
---
x: -0.8923522231905111
y: -0.40371134907012834
z: 0.16607050525205685
w: -0.11465293329189596
---
低头
---
x: -0.8070960049644686
y: -0.36529040268468144
z: -0.37963604992628835
w: 0.26652490947816504
---
左倾
---
x: -0.00674194008127619
y: -0.7917756444601525
z: 0.3535699532125574
w: -0.4980303943278207
---
右倾
---
x: -0.9824686050223255
y: 0.18610888719950164
z: -0.002180892640092067
w: -0.010684961353864503
---
左转
---
x: -0.9793246984120834
y: -0.20017144087785
z: 0.021217122314701322
w: -0.020111339061901785
---
右转
---
x: -0.9395180344753724
y: 0.3410725594100431
z: -0.029725197090456307
w: 0.009589396593448464
---
```

公共模块：

系统服务模块：

接口模块：

启动模块：

组件模块：



```
cd /ros2_ws/src
ros2 pkg create cerebellum_description \
  --build-type ament_cmake \
  --dependencies urdf xacro \
  --description "URDF description for cerebellum robot"
```

