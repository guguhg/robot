
## 小脑

### 负责模块

- 底盘电机驱动：截止/控制/读取单个电机(id,speed)、截止/控制/读取多个电机(map<id,speed>)

- BMS电池管理：电压、soc获取

- ros2_control框架：一个通用的控制框架，进行实时控制与极高移植性的框架实现。

  最小control.urdf.xacro： base_link、四个轮子link+joint、ros2_control joint

  硬件资源管理器：自动处理，管理各类硬件组件，根据URDF的描述文件决定加载哪些硬件组件

  控制器管理器：配置joint_state_broadcaster、controller

  控制器：/cmd_vel_safe回调存到realtime_tools::RealtimeBuffer、attitude_compensator姿态补偿（订阅/odometry/filtered）、

  IK逆运动学、/odom里程计发布 坐标系odom（无tf广播）、update

  ```
  ros2_control的核心是一个实时控制循环（Real-time Loop），它以固定频率执行 controller_manager的 read() -> update() -> write() 过程。
  任何在update()中调用的代码，都必须满足实时性要求：
  - 无动态内存分配（allocation-free）
  - 无锁（lock-free）
  - 执行时间有界（bounded execution time）
  
  attitude_compensator：订阅ekf融合后的/odometry/filtered, 获取四元数运算pitch、roll角，进行坡道速度补偿到twist消息
      comp_x = std::sin(pitch) * gravity_compensation_ * speed_factor;//+pitch 上坡加速、-pitch 下坡减速
      comp_y = std::sin(roll) * gravity_compensation_ * speed_factor;//+roll 右倾向左移，-roll左倾向右移
  IK:麦轮逆运动学，固定公式
  ```

  硬件组件：read、write接口实现

- 其他非ros2模块：公共配置加载器模块、日志模块

### 启动项

- ros2 launch bringup cerebellum_bringup.launch.py 综合启动

### 数据流

- /cmd_vel_safe+/odometry/filtered->attitude_compensator->IK->write->电机
- read->odom_updata->发布/odom(无tf变换)
