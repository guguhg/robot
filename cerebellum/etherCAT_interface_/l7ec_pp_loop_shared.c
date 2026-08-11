/*
 * ====================================================================
 * 文件名: l7ec_pp_loop_shared.c
 * 功能: 双雷赛 L7EC-400S 伺服电机 PP 模式 + 共享内存通信
 * 
 * 版本: 2.0 (共享内存版)
 * 
 * 核心改进:
 *   - 通过共享内存与外部进程（如 ros2_control）交换数据
 *   - 从共享内存读取目标位置，并将实际位置、状态写回
 *   - 保留原有使能流程、实时优化、抖动统计
 * 
 * 编译: 
 *   gcc -o l7ec_pp_loop_shared l7ec_pp_loop_shared.c ecat_shared_mem.c \
 *       -I/opt/etherlab/include -L/opt/etherlab/lib \
 *       -lethercat -lpthread -lrt -lm
 * 
 * 运行:
 *   sudo taskset -c 3 chrt -f 90 \
 *       LD_LIBRARY_PATH=/opt/etherlab/lib ./l7ec_pp_loop_shared
 * 
 * ====================================================================
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <ecrt.h>
#include <math.h>
#include <sys/mman.h>
#include <pthread.h>
#include <limits.h>
#include "ecat_shared_mem.h"   // 共享内存接口

/* ===== 系统参数 ===== */
#define PERIOD_NS 1000000
#define SLAVE_COUNT 2

#define VENDOR_ID  0x00004321
#define PRODUCT_CODE 0x000000b2

/* ===== IgH 主站核心句柄 ===== */
static ec_master_t *master = NULL;
static ec_domain_t *domain = NULL;
static ec_slave_config_t *sc[SLAVE_COUNT] = {NULL, NULL};

/* ===== PDO 偏移量 ===== */
static unsigned int off_ctrl[SLAVE_COUNT];
static unsigned int off_target[SLAVE_COUNT];
static unsigned int off_status[SLAVE_COUNT];
static unsigned int off_pos[SLAVE_COUNT];

/* ===== 共享内存指针 ===== */
static ecat_shared_data_t *shm = NULL;

/* 程序运行标志 */
volatile sig_atomic_t running = 1;

/* ===== 抖动统计 ===== */
typedef struct {
    long long count;
    double sum_cycle;
    double sum_dev;
    double sum_sq_dev;
    long long min_dev;
    long long max_dev;
} jitter_stats_t;

static jitter_stats_t stats = {0, 0.0, 0.0, 0.0, LLONG_MAX, LLONG_MIN};

/* ===== 从站状态机 ===== */
typedef struct {
    int step;
    int wait_cycles;
    int print_count;
    int32_t last_target;   // 记录上次目标位置，用于检测变化
} slave_state_t;

static slave_state_t slave_state[SLAVE_COUNT];

/* ===== 信号处理 ===== */
void signal_handler(int sig) {
    (void)sig;
    running = 0;
}

/* ===== 打印抖动统计 ===== */
#define JITTER_WARN_THRESHOLD_NS 50000  // 50 μs
void print_jitter_report(void) {
    if (stats.count == 0) return;
    double avg_cycle = stats.sum_cycle / stats.count;
    double avg_dev = stats.sum_dev / stats.count;
    double variance = (stats.sum_sq_dev / stats.count) - (avg_dev * avg_dev);
    double stddev = sqrt(variance > 0 ? variance : 0);

    printf("\n========== Jitter Statistics (ns) ==========\n");
    printf("  Samples:           %lld\n", stats.count);
    printf("  Average cycle:     %.2f ns\n", avg_cycle);
    printf("  Average deviation: %.2f ns\n", avg_dev);
    printf("  Std deviation:     %.2f ns\n", stddev);
    printf("  Min deviation:     %lld ns\n", stats.min_dev);
    printf("  Max deviation:     %lld ns\n", stats.max_dev);
    printf("============================================\n");
}

/* ===== 初始化从站状态 ===== */
void init_slave_states(void) {
    for (int i = 0; i < SLAVE_COUNT; i++) {
        slave_state[i].step = 0;
        slave_state[i].wait_cycles = 0;
        slave_state[i].print_count = 0;
        slave_state[i].last_target = 0;
    }
}

/*
 * ===== 单个从站的状态机 =====
 * 使能流程与之前相同，运动部分改为从共享内存读取目标位置。
 */
int run_slave_fsm(int idx, uint16_t *status, uint16_t *ctrl, int32_t *target, int32_t *pos) {
    slave_state_t *s = &slave_state[idx];
    int next_step = s->step;

    switch (s->step) {
        /* ---- 使能流程 ---- */
        case 0:
            *ctrl = 0x0000;
            next_step = 10;
            break;
        case 1:
            *ctrl = 0x0006;
            next_step = 11;
            break;
        case 2:
            *ctrl = 0x0007;
            next_step = 12;
            break;
        case 3:
            *ctrl = 0x000F;
            next_step = 13;
            break;

        /* ---- 等待确认 ---- */
        case 10:
            if ((*status & 0x0250) == 0x0250) next_step = 1;
            break;
        case 11:
            if ((*status & 0x0231) == 0x0231) next_step = 2;
            break;
        case 12:
            if ((*status & 0x0233) == 0x0233) next_step = 3;
            break;
        case 13:
            if ((*status & 0x0237) == 0x0237) {
                printf("[Slave %d] Operation enabled.\n", idx);
                next_step = 4;
                s->last_target = 0;  // 重置目标跟踪
            }
            break;

        /* ---- 运动控制：从共享内存读取目标位置 ---- */
        case 4:
            // 从共享内存前台缓冲区读取目标位置
            int32_t cmd_target = (int32_t)shm->axes[shm->write_idx][idx].target_position;

            // 如果目标位置发生变化，触发新位置运动
            if (cmd_target != s->last_target) {
                *target = cmd_target;          // 设置新目标
                *ctrl = 0x000F;                // 清除 bit4
                s->wait_cycles = 0;
                s->last_target = cmd_target;
                next_step = 5;
            }
            // 否则保持当前状态，不重复触发
            break;

        /* ---- 清除 bit4 等待 ---- */
        case 5:
            s->wait_cycles++;
            if (s->wait_cycles >= 2) {
                next_step = 6;
            }
            break;

        /* ---- 触发新位置 ---- */
        case 6:
            *ctrl = 0x001F;   // 置位 bit4，产生上升沿
            next_step = 7;
            s->wait_cycles = 0;
            break;

        /* ---- 等待运动开始 ---- */
        case 7:
            if ((*status & 0x1000) != 0x0000) {
                next_step = 8;
            } else {
                s->wait_cycles++;
                if (s->wait_cycles > 200) {
                    printf("[Slave %d] motion start timeout, retry\n", idx);
                    next_step = 4;
                }
            }
            break;

        /* ---- 等待目标到达 ---- */
        case 8:
            if (*status & 0x0400) {
                // 到达后清除 bit4，准备下一次触发
                *ctrl = 0x000F;
                next_step = 9;
                s->wait_cycles = 0;
            }
            break;

        /* ---- 确认 bit12 清除，返回等待新命令 ---- */
        case 9:
            if ((*status & 0x1000) == 0x0000) {
                next_step = 4;   // 回到步骤 4，等待新目标
            } else {
                s->wait_cycles++;
                if (s->wait_cycles > 200) {
                    printf("[Slave %d] bit12 clear timeout, retry\n", idx);
                    next_step = 4;
                }
            }
            break;

        default: break;
    }
    return next_step;
}

int main() {
    /* ---- 1. 创建共享内存 ---- */
    shm = ecat_shm_create();
    if (!shm) {
        fprintf(stderr, "Failed to create shared memory\n");
        return 1;
    }
    // 双缓冲写入默认目标位置
    {
        ecat_axis_data_t *ax = ecat_shm_writer_begin(shm);
        for (int i = 0; i < SLAVE_COUNT; i++) ax[i].target_position = 500000;
        ecat_shm_writer_commit(shm);
    }

    /* ---- 2. 实时性优化 ---- */
    if (mlockall(MCL_CURRENT | MCL_FUTURE) == -1)
        perror("mlockall");

    // 由chrt外部控制进程优先级
    // struct sched_param param = { .sched_priority = 80 };
    // if (pthread_setschedparam(pthread_self(), SCHED_FIFO, &param) != 0)
    //     perror("setschedparam");

    /* ---- 3. 请求主站 ---- */
    master = ecrt_request_master(0);
    if (!master) {
        fprintf(stderr, "request master failed\n");
        return 1;
    }

    /* ---- 4. SDO 配置 ---- */
    for (int i = 0; i < SLAVE_COUNT; i++) {
        uint8_t mode = 1; // PP
        if (ecrt_master_sdo_download(master, i, 0x6060, 0x00, &mode, sizeof(mode), 1000) < 0) {
            fprintf(stderr, "Failed to set mode\n");
            return -1;
        }
        // 读回确认
        uint8_t read_mode = 0;
        size_t read_size = 0;
        if (ecrt_master_sdo_upload(master, i, 0x6061, 0x00, &read_mode, sizeof(read_mode), &read_size, 1000) < 0) {
            fprintf(stderr, "Failed to read mode\n");
            return -1;
        }
        if (read_mode != mode) {
            fprintf(stderr, "Mode mismatch\n");
            return -1;
        }

        // 速度参数
        uint32_t vel = 100000, acc = 100000, dec = 100000;
        if (ecrt_master_sdo_download(master, i, 0x6081, 0x00, &vel, sizeof(vel), 1000) < 0)
            fprintf(stderr, "Slave %d: Failed to set velocity\n", i);
        if (ecrt_master_sdo_download(master, i, 0x6083, 0x00, &acc, sizeof(acc), 1000) < 0)
            fprintf(stderr, "Slave %d: Failed to set acceleration\n", i);
        if (ecrt_master_sdo_download(master, i, 0x6084, 0x00, &dec, sizeof(dec), 1000) < 0)
            fprintf(stderr, "Slave %d: Failed to set deceleration\n", i);
    }

    /* ---- 5. 创建 domain ---- */
    domain = ecrt_master_create_domain(master);
    if (!domain) {
        fprintf(stderr, "create domain failed\n");
        return 1;
    }

    /* ---- 6. 获取从站配置 ---- */
    for (int i = 0; i < SLAVE_COUNT; i++) {
        sc[i] = ecrt_master_slave_config(master, 0, i, VENDOR_ID, PRODUCT_CODE);
        if (!sc[i]) {
            fprintf(stderr, "slave %d config failed\n", i);
            return 1;
        }
    }

    /* ---- 7. 注册 PDO entry ---- */
    int ret;
    for (int i = 0; i < SLAVE_COUNT; i++) {
        ret = ecrt_slave_config_reg_pdo_entry(sc[i], 0x6040, 0x00, domain, NULL);
        if (ret < 0) { fprintf(stderr, "slave %d: reg 0x6040 failed\n", i); return 1; }
        off_ctrl[i] = ret;

        ret = ecrt_slave_config_reg_pdo_entry(sc[i], 0x607A, 0x00, domain, NULL);
        if (ret < 0) { fprintf(stderr, "slave %d: reg 0x607A failed\n", i); return 1; }
        off_target[i] = ret;

        ret = ecrt_slave_config_reg_pdo_entry(sc[i], 0x6041, 0x00, domain, NULL);
        if (ret < 0) { fprintf(stderr, "slave %d: reg 0x6041 failed\n", i); return 1; }
        off_status[i] = ret;

        ret = ecrt_slave_config_reg_pdo_entry(sc[i], 0x6064, 0x00, domain, NULL);
        if (ret < 0) { fprintf(stderr, "slave %d: reg 0x6064 failed\n", i); return 1; }
        off_pos[i] = ret;
    }

    /* ---- 8. 激活主站 ---- */
    if (ecrt_master_activate(master)) {
        fprintf(stderr, "activate master failed\n");
        return 1;
    }

    /* ---- 9. 获取 domain 指针 ---- */
    uint8_t *domain_pd = ecrt_domain_data(domain);
    if (!domain_pd) {
        fprintf(stderr, "get domain data failed\n");
        return 1;
    }

    /* 打印偏移量 */
    for (int i = 0; i < SLAVE_COUNT; i++) {
        printf("Slave %d offsets: ctrl=%u, target=%u, status=%u, pos=%u\n",
               i, off_ctrl[i], off_target[i], off_status[i], off_pos[i]);
    }

    /* ---- 10. 标记共享内存就绪 ---- */
    shm->flags |= ECAT_FLAG_READY;
    printf("Shared memory ready. Waiting for commands...\n");

    /* ---- 11. 注册信号处理 ---- */
    signal(SIGINT, signal_handler);

    /* ---- 12. 初始化状态机 ---- */
    init_slave_states();

    /* ---- 13. 准备定时 ---- */
    struct timespec ts = {0, 0};
    clock_gettime(CLOCK_REALTIME, &ts);
    printf("Starting cyclic operation (1ms) with 2 slaves...\n");

    struct timespec prev_ts, curr_ts;
    clock_gettime(CLOCK_REALTIME, &prev_ts);

    /* ---- 14. 主循环 ---- */
    while (running) {
        ts.tv_nsec += PERIOD_NS;
        if (ts.tv_nsec >= 1000000000) { ts.tv_sec++; ts.tv_nsec -= 1000000000; }
        clock_nanosleep(CLOCK_REALTIME, TIMER_ABSTIME, &ts, NULL);

        /* 抖动测量 */
        clock_gettime(CLOCK_REALTIME, &curr_ts);
        long long cycle_ns = (curr_ts.tv_sec - prev_ts.tv_sec) * 1000000000LL +
                             (curr_ts.tv_nsec - prev_ts.tv_nsec);
        long long dev_ns = cycle_ns - PERIOD_NS;

        if (dev_ns > JITTER_WARN_THRESHOLD_NS || dev_ns < -JITTER_WARN_THRESHOLD_NS) {
            struct timespec now;
            clock_gettime(CLOCK_REALTIME, &now);
            int cpu = sched_getcpu();
            printf("[JITTER WARNING] cycle=%lld, dev=%+lld ns, cpu=%d\n",
                   stats.count, dev_ns, cpu);
        }

        if (stats.count == 0) {
            stats.min_dev = dev_ns; stats.max_dev = dev_ns;
        } else {
            if (dev_ns < stats.min_dev) stats.min_dev = dev_ns;
            if (dev_ns > stats.max_dev) stats.max_dev = dev_ns;
        }
        stats.count++;
        stats.sum_cycle += cycle_ns;
        stats.sum_dev += dev_ns;
        stats.sum_sq_dev += (double)dev_ns * dev_ns;

        if (stats.count % 10000 == 0) {
            double avg_dev = stats.sum_dev / stats.count;
            printf("[JITTER] samples=%lld, avg_dev=%.2f ns, max_dev=%lld ns\n",
                   stats.count, avg_dev, stats.max_dev);
        }

        prev_ts = curr_ts;

        /* EtherCAT 接收 */
        ecrt_master_receive(master);
        ecrt_domain_process(domain);

        /* 运行每个从站的状态机, 双缓冲写入后台 */
        ecat_axis_data_t *ax = ecat_shm_writer_begin(shm);

        for (int i = 0; i < SLAVE_COUNT; i++) {
            uint16_t *status = (uint16_t *)(domain_pd + off_status[i]);
            uint16_t *ctrl   = (uint16_t *)(domain_pd + off_ctrl[i]);
            int32_t  *target = (int32_t  *)(domain_pd + off_target[i]);
            int32_t  *pos    = (int32_t  *)(domain_pd + off_pos[i]);

            // 执行状态机
            slave_state[i].step = run_slave_fsm(i, status, ctrl, target, pos);

            // 写入后台缓冲区
            ax[i].actual_position = *pos;
            ax[i].status_word = *status;
            ax[i].is_enabled = ((*status & 0x0237) == 0x0237);
            ax[i].target_reached = (*status & 0x0400) != 0;
            ax[i].is_fault = (*status & 0x0008) != 0;
        }

        // 翻转, 新状态对读者可见
        ecat_shm_writer_commit(shm);

        /* 发送 */
        ecrt_domain_queue(domain);
        ecrt_master_send(master);
    }

    /* ---- 退出 ---- */
    shm->flags &= ~ECAT_FLAG_READY;
    print_jitter_report();
    ecat_shm_close(shm);
    ecrt_release_master(master);
    return 0;
}