# IMU链路重构

```
IMU硬件
    │
    ▼
drivers/IMU (驱动层)
作用: 通过 driver_port.h 的 IMU_DATA_GET 宏读取原始数据
    │
    ▼
imu_dri (接口层) 
作用: 
  1. 加载 imu_calib.yaml (SM矩阵 + bias)
  2. 应用校准: calibrated = SM × (raw - bias)
  3. 单位转换: deg/s → rad/s
  4. 发布 /imu/data_raw (已经是ROS标准坐标系)
    │
    ▼
imu_tools_node (算法层)
作用: 互补滤波融合 (加速度+陀螺仪 → 四元数)
     输出 /imu/data (四元数)
    │
    ├─────────────────┬─────────────────┐
    │                 │                 │
    ▼                 ▼                 ▼
attitude_comp    robot_localization   其他消费者
(姿态补偿)       (EKF融合 - 可选)     (rviz显示)
```

# 测试

直接在命令行测试不同的 TF 组合，看哪个让加速度和角速度都正确：

```
sudo docker start -ai cerebellum
cd /ros2_ws
colcon build --symlink-install
source install/setup.bash
ros2 launch bringup dri_bringup.launch.py
ros2 launch bringup algorithms_bringup.launch.py
```



```
cd /ros2_ws
colcon build --symlink-install
source install/setup.bash
ros2 launch bringup dri_bringup.launch.py
ros2 launch bringup imu_bringup.launch.py

# 1. 查看原始数据（imu_dri 输出，IMU 物理坐标系）
ros2 topic echo /imu/data_raw --field linear_acceleration
水平静止
---
x: 0.0435791015625
y: -0.066650390625
z: 0.9012451171875
---
倒立
---
x: 0.0765380859375
y: -0.14306640625
z: -1.08984375
---
抬头（前方抬起，后方下压）
---
x: 0.0570068359375
y: -1.01025390625
z: 0.2618408203125
---
低头（后方抬起，前方下压）
---
x: 0.06005859375
y: 0.849853515625
z: 0.2960205078125
---
左倾（左侧向下压，右侧抬起）
---
x: -0.9117431640625
y: -0.08447265625
z: 0.1346435546875
---
右倾（右侧向下压，左侧抬起）
---
x: 0.9302978515625
y: -0.0811767578125
z: 0.3729248046875
---

# 2. 查看变换后数据（imu_transformer 输出，ROS 标准坐标系）
ros2 topic echo /imu/data_corrected --field linear_acceleration
水平静止
---
x: -0.07482910156249999
y: -0.046386718750000014
z: 0.900634765625
---
倒立
---
x: -0.08239746093749999
y: -0.07031250000000001
z: -1.091552734375
---
抬头（前方抬起，后方下压）
---
x: 1.001953125
y: 0.04699707031249978
z: 0.284912109375
---
低头（后方抬起，前方下压）
---
x: -0.8746337890625
y: 0.050659179687500194
z: 0.234619140625
---
左倾（左侧向下压，右侧抬起，Y+指地）
---
x: 0.0876464843749998
y: -0.864013671875
z: 0.2978515625
---
右倾（右侧向下压，左侧抬起，Y+指天）
---
x: 0.0900878906250002
y: 0.8804931640625
z: 0.45263671875
---

# 3. 查看话题是否都有发布者
ros2 topic info /imu/data_raw
ros2 topic info /imu/data_corrected
ros2 topic info /imu/data

# 4. 查看 TF 变换
ros2 run tf2_ros tf2_echo imu_link_ros imu_link

# 5. 查看所有活跃话题
ros2 topic list

# 6. 角速度验证
ros2 topic echo /imu/data_corrected --field angular_velocity
静止（有零漂）
---
x: -0.0744492039084435
y: -0.2709350883960724
z: -0.008590292185544968
---
左转（逆时针）
---
x: -0.34899768233299266
y: -0.510542869567871
z: 1.1169766187667847
---
右转（顺时针）
---
x: -0.055632367730140755
y: -0.3223404884338379
z: -1.1170107126235962
---
抬头（前方抬起）
---
x: 0.5306209921836851
y: -1.1170107126235964
z: 0.07134714722633362
---
低头（前方下压）
---
x: 0.057609498500824224
y: 1.1169766187667847
z: -0.05706408619880676
---
右滚转（右侧下压 Y+指天）
---
x: 0.9580221176147459
y: -0.9293878078460696
z: -0.35987189412117004
---
左滚转（左侧下压 Y+指地）
---
x: -0.997701108455658
y: 0.13724014163017295
z: -0.2797299027442932
---
```

