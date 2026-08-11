┌──────────────────────────────────────────────────────────────────────┐
│                         共享内存 (Shared Memory)                    │
│  ┌─────────────────────────────────────────────────────────────┐    │
│  │  struct ecat_shared_data {                                  │    │
│  │      uint32_t timestamp;                                    │    │
│  │      double target_pos[NUM_AXES];   // 命令（ros2_control 写入）│    │
│  │      double actual_pos[NUM_AXES];   // 状态（EtherCAT 写入） │    │
│  │      uint16_t status_word[NUM_AXES];                        │    │
│  │      uint16_t control_word[NUM_AXES];                       │    │
│  │      ...                                                    │    │
│  │  }                                                          │    │
│  └─────────────────────────────────────────────────────────────┘    │
└─────────────────┬───────────────────────────────┬──────────────────┘
                  │                               │
                  ▼                               ▼
┌─────────────────────────────────┐   ┌─────────────────────────────────┐
│    EtherCAT 实时进程            │   │   ros2_control 节点             │
│    (绑定隔离核心，优先级 90)   │   │   (绑定控制核心，优先级 50)     │
│                                 │   │                                 │
│  ┌─────────────────────────┐   │   │  ┌─────────────────────────┐   │
│  │ 1ms 周期循环:           │   │   │  │  hardware_interface:    │   │
│  │ - 读取共享内存命令       │◄──┼───┼──│  - read()  读共享内存   │   │
│  │ - ecrt_master_receive() │   │   │  │  - write() 写共享内存   │   │
│  │ - 状态机使能/运动       │   │   │  └─────────────────────────┘   │
│  │ - 写入共享内存状态      │───┼──►│                                 │
│  │ - ecrt_master_send()    │   │   │  ┌─────────────────────────┐   │
│  └─────────────────────────┘   │   │  │  Controller (如 JTC)    │   │
│                                 │   │  │  读取状态 → 计算控制   │   │
│  CPU3 (isolcpus)              │   │  │  写入命令                │   │
└─────────────────────────────────┘   │  └─────────────────────────┘   │
                                      │                                 │
                                      │  CPU2                          │
                                      └─────────────────────────────────┘


1.PREEMPT-RT 内核，将 Linux 变为可抢占实时内核，降低调度延迟
2.CPU 频率固定为 performance，避免动态调频引入的延迟尖峰
3.禁用 timer_migration，防止定时器在不同 CPU 核心间迁移
4.关闭 irqbalance，防止中断被随机迁移到其他核心
5.进程绑定CPU核心
6.网卡中断亲和性,将网卡中断绑定到非实时核心，避免抢占实时线程
7.永久生效配置
8.提供.h接口，内容为IPC读写交换数据。
9.EtherCAT主循环，绑定CPU3核心，设置优先级 80，锁定内存防止缺页，实时循环优化（使用绝对时间等待、减少IO交互）
10.（专用网卡驱动（如 igb））待做

任务1：PREEMPT-RT 内核已完成


任务2：CPU 频率固定为 performance
# 查看当前所有 CPU 的 scaling_governor
cat /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor
# 如果目录存在，直接写入 performance
echo performance | sudo tee /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor


任务3：禁用 timer_migration
# 查看当前值
sudo sysctl kernel.timer_migration
# 设为 0
sudo sysctl -w kernel.timer_migration=0



任务4：关闭 irqbalance，防止中断被随机迁移到其他核心
# 停止并禁用
sudo systemctl stop irqbalance
Failed to stop irqbalance.service: Unit irqbalance.service not loaded. # 无需操作，没装
sudo systemctl disable irqbalance
# 确认已停止
sudo systemctl status irqbalance



任务5：进程绑定CPU核心3
# 引导系统为extlinux.conf情况下
sudo vim /boot/extlinux/extlinux.conf
找到 append 行，在行末添加：isolcpus=3 nohz_full=3 rcu_nocbs=3
sudo reboot
cat /proc/cmdline | grep isolcpus # 应该看到 isolcpus=3

# 引导系统U-Boot 脚本（boot.scr）引导
sudo vim /boot/boot.cmd
在 setenv bootargs 行末尾添加：isolcpus=3 nohz_full=3 rcu_nocbs=3
sudo mkimage -C none -A arm64 -T script -d /boot/boot.cmd /boot/boot.scr
sudo reboot
cat /proc/cmdline | grep isolcpus # 现在应该能看到 isolcpus=3

# 进程绑定 CPU 核心（在运行程序时）
sudo taskset -c 3 chrt -f 90 \
    LD_LIBRARY_PATH=/opt/etherlab/lib \
    ./l7ec_pp_loop






# 持久化脚本
sudo vim /etc/systemd/system/ecat-optimize.service
内容如下：
[Unit]
Description=EtherCAT Real-time Optimizations
After=network.target

[Service]
Type=oneshot
RemainAfterExit=yes
ExecStart=/usr/local/bin/ecat-optimize.sh
ExecStop=/usr/local/bin/ecat-optimize.sh stop

[Install]
WantedBy=multi-user.target

sudo vim /usr/local/bin/ecat-optimize.sh
内容：
#!/bin/bash
# /usr/local/bin/ecat-optimize.sh
# EtherCAT 实时优化脚本

case "$1" in
    start|"")
        echo "=== Applying EtherCAT real-time optimizations ==="

        # 1. CPU 频率固定为 performance
        echo performance | tee /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor

        # 2. 网卡中断亲和性 - 将 eth0 绑定到 CPU 0-1 (非隔离核心)
        IRQ=$(cat /proc/interrupts | grep eth0 | awk -F: '{print $1}' | tr -d ' ')
        if [ -n "$IRQ" ]; then
            # 绑定到 CPU 0 和 CPU 1 (掩码 0b0011 = 0x03)
            echo "03" > /proc/irq/$IRQ/smp_affinity
            echo "eth0 IRQ $IRQ bound to CPU 0-1"
        fi

        echo "=== Optimizations applied ==="
        ;;

    stop)
        # 恢复 CPU 频率为 interactive（可选）
        echo interactive | tee /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor
        echo "=== Optimizations reverted ==="
        ;;

    *)
        echo "Usage: $0 {start|stop}"
        exit 1
        ;;
esac

exit 0

sudo chmod +x /usr/local/bin/ecat-optimize.sh
sudo systemctl daemon-reload
sudo systemctl enable ecat-optimize.service
sudo systemctl start ecat-optimize.service
sudo systemctl status ecat-optimize.service


timer_migration 永久化
echo "kernel.timer_migration=0" | sudo tee -a /etc/sysctl.conf
sudo sysctl -p


创建任务启动脚本 ~/etherCAT_interface/start_ecat.sh
chmod +x ~/etherCAT_interface/start_ecat.sh

验证
sudo reboot
# 1. 验证 CPU 频率
cat /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor

# 2. 验证 timer_migration
sysctl kernel.timer_migration

# 3. 验证网卡中断亲和性
cat /proc/interrupts | grep eth0
cat /proc/irq/*/smp_affinity

# 4. 查看服务状态
systemctl status ecat-optimize.service


# 1. 确保 ecat_shared_mem.c 和 ecat_shared_mem.h 在同一目录
# 2. 编译
gcc -o l7ec_pp_loop_shared l7ec_pp_loop_shared.c ecat_shared_mem.c \
    -I/opt/etherlab/include -L/opt/etherlab/lib \
    -lethercat -lpthread -lrt -lm

# 3. 运行（绑定 CPU 3，实时优先级 90）
sudo taskset -c 3 chrt -f 90 \
    LD_LIBRARY_PATH=/opt/etherlab/lib \
    ./l7ec_pp_loop_shared
    