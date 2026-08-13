/*
 * ====================================================================
 * 文件名: ecat_shared_mem.h
 * 功能: EtherCAT 与 ros2_control 共享内存接口
 * 版本: 1.0
 * 
 * 用法:
 *   - EtherCAT 实时进程: ecat_shm_create() 创建共享内存
 *   - ros2_control 节点: ecat_shm_open_existing() 打开已存在的共享内存
 *   然后通过 axes 数组读写每个轴的数据。
 * ====================================================================
 */


/*
 ros2_control 硬件组件中如何使用
 #include "ecat_shared_mem.h"

// 在 on_activate() 或 on_configure() 中：
shm_ = ecat_shm_open_existing();
if (!shm_) {
    RCLCPP_ERROR(...);
    return CallbackReturn::ERROR;
}
if (!ecat_shm_wait_ready(shm_, 5000)) {
    RCLCPP_ERROR(...);
    return CallbackReturn::ERROR;
}

// 在 read() 中：
for (int i = 0; i < num_axes_; i++) {
    state_pos_[i] = shm_->axes[i].actual_position;
    state_vel_[i] = shm_->axes[i].actual_velocity;
    state_effort_[i] = shm_->axes[i].actual_torque;
}

// 在 write() 中：
for (int i = 0; i < num_axes_; i++) {
    shm_->axes[i].target_position = cmd_pos_[i];
    shm_->axes[i].target_velocity = cmd_vel_[i];
    shm_->axes[i].target_torque = cmd_effort_[i];
    shm_->axes[i].control_word = 0x000F; // 保持使能
}

*/
#ifndef ECAT_SHARED_MEM_H
#define ECAT_SHARED_MEM_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ===== 配置 ===== */
#define ECAT_SHM_NAME  "/ecat_shm"         // 共享内存名称
#define ECAT_SHM_SIZE  4096                // 共享内存大小（字节）
#define ECAT_MAX_AXES  8                   // 最大轴数
#define ECAT_MAGIC     0xEC4A7C00          // 魔数验证

/* ===== 每个轴的数据 ===== */
typedef struct {
    /* ----- 命令（ros2_control → EtherCAT）----- */
    double target_position;          // 目标位置（指令单位）
    double target_velocity;          // 目标速度（指令单位/秒）
    double target_torque;            // 目标转矩（% 或 Nm）
    uint16_t control_word;           // 控制字（直接控制）
    uint8_t  mode;                   // 操作模式 (1=PP, 8=CSP, 9=CSV), 具体见手册

    /* ----- 状态（EtherCAT → ros2_control）----- */
    double actual_position;          // 实际位置（指令单位）
    double actual_velocity;          // 实际速度（指令单位/秒）
    double actual_torque;            // 实际转矩（% 或 Nm）
    uint16_t status_word;            // 状态字
    uint16_t error_code;             // 错误码 (0x603F)
    bool     is_enabled;             // 是否使能
    bool     is_fault;               // 是否有故障
    bool     target_reached;         // 目标是否到达
    bool     is_homed;               // 是否已回零
} ecat_axis_data_t;

/* ===== 共享内存总结构 ===== */
typedef struct {
    /* ----- 元数据 ----- */
    uint32_t magic;                  // 魔数 0xEC4A7C00
    uint32_t version;                // 接口版本 (3 = 双缓冲)
    uint32_t timestamp_ms;           // 时间戳（毫秒）
    uint32_t cycle_count;            // 周期计数
    uint32_t num_axes;               // 实际轴数

    /* ----- 状态标志 ----- */
    uint32_t flags;                  // 见下方标志位定义
    uint32_t fault_code;             // 全局故障码（0=无）

    /* ----- 双缓冲区: axes[0] + axes[1], 各 ECAT_MAX_AXES 个轴 ----- */
    ecat_axis_data_t axes[2][ECAT_MAX_AXES];

    /* ----- 原子索引 ----- */
    volatile uint32_t write_idx;     // 当前可读的缓冲区 (0 或 1)
    volatile uint32_t seq;           // 单调递增序号, 每次写入+1
} ecat_shared_data_t;

/* ===== 标志位 ===== */
#define ECAT_FLAG_READY         (1 << 0)   // EtherCAT 已进入 OP
#define ECAT_FLAG_ALL_ENABLED   (1 << 1)   // 所有轴已使能
#define ECAT_FLAG_FAULT         (1 << 2)   // 有故障

/* ===== API 函数 ===== */

/**
 * 创建共享内存（EtherCAT 进程调用）
 * @return 指针，失败返回 NULL
 */
ecat_shared_data_t* ecat_shm_create(void);

/**
 * 打开已存在的共享内存（ros2_control 节点调用）
 * @return 指针，失败返回 NULL
 */
ecat_shared_data_t* ecat_shm_open_existing(void);

/**
 * 关闭共享内存（进程退出时调用）
 */
void ecat_shm_close(ecat_shared_data_t *shm);

/**
 * 等待 EtherCAT 就绪（ros2_control 调用）
 * @param shm       共享内存指针
 * @param timeout_ms 超时毫秒，0 表示无限等待
 * @return true 就绪，false 超时
 */
bool ecat_shm_wait_ready(ecat_shared_data_t *shm, int timeout_ms);

/* ===== 双缓冲读写 API (v3.0) ===== */

/** 写者: 获取后台缓冲区, 写完调 commit */
ecat_axis_data_t* ecat_shm_writer_begin(ecat_shared_data_t *shm);
/** 写者: 翻转 write_idx + seq++, 新数据对读者可见 */
void ecat_shm_writer_commit(ecat_shared_data_t *shm);
/** 读者: 锁定前台缓冲区, 保存 seq */
ecat_axis_data_t* ecat_shm_reader_begin(ecat_shared_data_t *shm, uint32_t *seq);
/** 读者: 校验 seq, 一致返回 true */
bool ecat_shm_reader_end(ecat_shared_data_t *shm, uint32_t saved_seq);

#ifdef __cplusplus
}
#endif

#endif /* ECAT_SHARED_MEM_H */