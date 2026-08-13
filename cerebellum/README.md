## 小脑

### 负责模块

#### 1. EtherCAT 伺服电机驱动（底盘执行）

- IgH EtherCAT Master 主站，控制雷赛 L7EC-400S 伺服
- 1ms 周期实时控制（DC 分布式时钟同步），支持 PP / CSP / CSV 多模式
- 抖动优化：标准差 86.6μs → 6.3μs（13.7 倍），最大抖动 10ms → 308μs（32.5 倍）
- 三层优化：系统级（PREEMPT-RT 补丁、CPU 调频固定 performance、禁用 timer_migration / irqbalance、网卡中断绑定非实时核）+ 进程级（taskset 绑核 CPU3、chrt -f 90 设 SCHED_FIFO）+ 代码级（mlockall 锁内存防 Page Fault、绝对时间等待、移除 IO 操作）

#### 2. EtherCAT ↔ ros2_control 共享内存 IPC（零拷贝无锁）

- 目标：打通 IgH 主进程与 ros2_control 进程的实时数据交换

- 为什么不用锁：互斥锁会导致优先级反转——低优先级任务持锁时被普通 ROS2 节点抢占，IgH 1ms 循环等锁超时，触发 EtherCAT 看门狗伺服急停

- 为什么不用其他 IPC：管道 / Socket / 队列都存在数据拷贝；DDS 可能序列化并动态申请内存（malloc 耗时不确定，实时循环禁止）

- 方案：`shm_open + mmap` 映射同一块内存到两个进程，零拷贝

- 一致性保障（三层）：

  1. **双缓冲 + 原子索引**：`axes[2][N]` 两份数据，写者写后台缓冲后翻转 `write_idx`，读者只读前台——杜绝"写一半就读"的数据撕裂
  2. **seq 序号校验**：每次 commit 后 `seq++`，读者 `reader_begin` 记录 seq，`reader_end` 比对，不一致则重试
  3. **单写者模型**：每个字段只有一方写（命令由 ros2_control 写，状态由 EtherCAT 进程写）

- 内存屏障：`__sync_synchronize()`（ARM 上编译为 DMB ISH）确保翻转索引前所有写入全局可见

  ```c
  // 写者（EtherCAT 进程）
  ecat_axis_data_t *ax = ecat_shm_writer_begin(shm);  // 拿后台缓冲
  ax[i].actual_position = *pos;                       // 填数据
  ax[i].status_word = *status;
  ecat_shm_writer_commit(shm);                        // 翻转 write_idx + seq++
  
  // 读者（ros2_control）
  uint32_t seq;
  ecat_axis_data_t *ax = ecat_shm_reader_begin(shm, &seq);  // 拿前台缓冲
  double pos = ax[i].actual_position;
  bool consistent = ecat_shm_reader_end(shm, seq);          // 校验 seq
  ```

#### 3. BMS 电池管理

- 电压、SOC 获取

#### 4. ros2_control 框架

通用的实时控制框架，实现实时控制与极高移植性。

- 最小 `control.urdf.xacro`：base_link、四个轮子 link+joint、ros2_control joint

- 硬件资源管理器：自动管理各类硬件组件，根据 URDF 决定加载哪些硬件组件

- 控制器管理器：配置 joint_state_broadcaster、controller

- 控制器：`/cmd_vel_safe` 回调存到 `realtime_tools::RealtimeBuffer`、attitude_compensator 姿态补偿（订阅 `/odometry/filtered`）、IK 逆运动学、`/odom` 里程计发布（坐标系 odom，无 tf 广播）、update

  ```
  ros2_control 的核心是一个实时控制循环（Real-time Loop），以固定频率执行
  controller_manager 的 read() -> update() -> write() 过程。
  任何在 update() 中调用的代码，都必须满足实时性要求：
  - 无动态内存分配（allocation-free）
  - 无锁（lock-free）
  - 执行时间有界（bounded execution time）
  
  attitude_compensator：订阅 ekf 融合后的 /odometry/filtered，获取四元数运算
  pitch、roll 角，进行坡道速度补偿到 twist 消息
      comp_x = std::sin(pitch) * gravity_compensation_ * speed_factor; // +pitch 上坡加速、-pitch 下坡减速
      comp_y = std::sin(roll)  * gravity_compensation_ * speed_factor; // +roll 右倾向左移，-roll 左倾向右移
  IK：麦轮逆运动学，固定公式
  ```

- 硬件组件：read、write 接口实现

#### 5. 其他非 ros2 模块

- 公共配置加载器模块、日志模块

---

### 启动项

#### Docker 容器启动

```bash
sudo docker run -itd --name cerebellum \
  -v ~/robot/brain/ros2_ws:/ros2_ws \
  -v ~/.ssh:/root/.ssh:ro \
  --privileged \
  --volume /dev/bus/usb:/dev/bus/usb \
  cerebellum:ros2-humble-full-ptp-cerebellum

sudo docker network connect robot_net cerebellum
sudo docker exec -it cerebellum /bin/bash
```

#### EtherCAT 主站进程（实时）

```bash
# 启动 IgH 主站（绑核 CPU3，实时优先级 90）
sudo taskset -c 3 chrt -f 90 \
    LD_LIBRARY_PATH=/opt/etherlab/lib \
    ./l7ec_pp_loop_shared
# 或使用封装脚本 start_ecat.sh
```

#### ros2_control 启动

```bash
ros2 launch bringup cerebellum_bringup.launch.py
```

#### 开机自启动

```bash
sudo systemctl enable start-cerebellum.service
sudo systemctl start start-cerebellum.service
```

---

### 数据流

```
                    ┌─────────────────────────────────────────┐
                    │           ros2_control 节点 (CPU2)       │
                    │                                         │
  /cmd_vel_safe ──► │  read() 读共享内存状态                   │
  /odometry/filtered│  controller: attitude_compensator        │
                    │       → IK 麦轮逆运动学                 │
                    │  write() 写共享内存命令                  │
                    └───────────────┬─────────────────────────┘
                                    │ 共享内存 /ecat_shm (mmap 零拷贝)
                                    │   命令：target_position 等
                                    │   状态：actual_position 等
                                    ▼
                    ┌─────────────────────────────────────────┐
                    │      EtherCAT 实时进程 (CPU3, prio 90)   │
                    │                                         │
                    │  读共享内存命令 → ecrt_master_receive()   │
                    │  → 状态机使能/运动 → 写共享内存状态       │
                    │  → ecrt_master_send()                    │
                    │  （1ms 周期，DC 同步）                    │
                    └───────────────┬─────────────────────────┘
                                    │ EtherCAT 帧
                                    ▼
                    雷赛 L7EC-400S 伺服电机（×2）
```

**两条主线**：

1. **命令下发**：`/cmd_vel_safe` → ros2_control 控制器（姿态补偿 + IK）→ 写共享内存 → EtherCAT 进程读命令 → PDO 周期下发到伺服
2. **状态反馈**：伺服状态（位置/速度）→ EtherCAT 进程写共享内存 → ros2_control `read()` 读取 → 发布 `/odom`（无 tf 变换）

**EtherCAT 收发循环**（1ms 周期）：

```
ecrt_master_receive → ecrt_domain_process → 读状态字/写控制字+位置 → ecrt_domain_queue → ecrt_master_send
```
