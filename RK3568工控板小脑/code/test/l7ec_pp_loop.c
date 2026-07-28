/*
 * ====================================================================
 * 文件名: l7ec_pp_loop.c
 * 功能: 单雷赛 L7EC-400S PP 模式往复运动 + 实时抖动统计
 *       控制电机在 0~500000 之间往复运动，测量 1ms 周期抖动。
 *
 * 抖动定义：
 *   - 周期时间 (cycle_ns) = 相邻两次循环开始的实际时间间隔
 *   - 偏差 (dev_ns) = cycle_ns - 1,000,000 (ns)
 *   - 统计所有偏差的平均值、标准差、最小值、最大值
 *
 * 实时优化：
 *   - 主线程设置为 SCHED_FIFO，优先级 80
 *   - 内存锁定（mlockall）防止交换和缺页异常
 *   - 实时循环中减少调试打印，仅每 10000 周期输出一次抖动统计
 *
 * 编译: gcc -o l7ec_pp_loop l7ec_pp_loop.c \
 *           -I/opt/etherlab/include -L/opt/etherlab/lib \
 *           -lethercat -lpthread -lrt -lm
 * 运行: sudo LD_LIBRARY_PATH=/opt/etherlab/lib ./l7ec_pp_loop
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

/* ===== 周期时间 ===== */
#define PERIOD_NS 1000000              // 1ms 周期

/* ===== 从站识别参数（从 ethercat slaves -v 获取） ===== */
#define VENDOR_ID  0x00004321          // 雷赛厂商 ID
#define PRODUCT_CODE 0x000000b2        // L7EC-400S 产品代码

/* ===== IgH 主站核心句柄 ===== */
static ec_master_t *master = NULL;      // 主站实例
static ec_domain_t *domain = NULL;      // 过程数据域
static ec_slave_config_t *sc = NULL;    // 从站配置

/* ===== PDO 偏移量 ===== */
static unsigned int off_ctrl;           // 控制字 0x6040
static unsigned int off_target;         // 目标位置 0x607A
static unsigned int off_status;         // 状态字 0x6041
static unsigned int off_pos;            // 实际位置 0x6064

volatile sig_atomic_t running = 1;

/* ===== 抖动统计结构 ===== */
typedef struct {
    long long count;          // 统计样本数
    double sum_cycle;         // 实际周期总和（纳秒）
    double sum_dev;           // 偏差总和（实际周期 - PERIOD_NS）
    double sum_sq_dev;        // 偏差平方和（用于标准差）
    long long min_dev;        // 最小偏差（纳秒）
    long long max_dev;        // 最大偏差（纳秒）
} jitter_stats_t;

static jitter_stats_t stats = {0, 0.0, 0.0, 0.0, LLONG_MAX, LLONG_MIN};

/* ===== 状态机状态 ===== */
typedef struct {
    int step;                 // 当前状态机步骤
    int pos_index;            // 当前目标索引（0→500000, 1→0）
    int wait_cycles;          // 等待计数器（用于超时）
    int print_count;          // 打印计数器（控制日志频率）
} slave_state_t;

static slave_state_t slave_state;

/* ===== 目标位置序列 ===== */
static int32_t target_positions[] = {500000, 0};

/* ===== 信号处理函数 ===== */
void signal_handler(int sig) {
    (void)sig;
    running = 0;
}

/* ===== 打印抖动统计报告 ===== */
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

/* ===== 初始化状态机 ===== */
void init_slave_state(void) {
    slave_state.step = 0;
    slave_state.pos_index = 0;
    slave_state.wait_cycles = 0;
    slave_state.print_count = 0;
}

/*
 * ===== 状态机函数 =====
 * 实现 CiA 402 使能流程（0x0000 → 0x0006 → 0x0007 → 0x000F）
 * 以及 0~500000 往复运动逻辑
 */
int run_slave_fsm(uint16_t *status, uint16_t *ctrl, int32_t *target, int32_t *pos) {
    slave_state_t *s = &slave_state;
    int next_step = s->step;

    switch (s->step) {
        /* ---- 使能流程：写入阶段 ---- */
        case 0:
            *ctrl = 0x0000;          // 清除控制字，准备使能
            next_step = 10;
            break;
        case 1:
            *ctrl = 0x0006;          // Shutdown 命令
            next_step = 11;
            break;
        case 2:
            *ctrl = 0x0007;          // Switch On 命令
            next_step = 12;
            break;
        case 3:
            *ctrl = 0x000F;          // Enable Operation 命令
            next_step = 13;
            break;

        /* ---- 使能流程：等待确认阶段 ---- */
        case 10:
            if ((*status & 0x0250) == 0x0250)   // 检查状态字是否达到 0x0250
                next_step = 1;
            break;
        case 11:
            if ((*status & 0x0231) == 0x0231)   // 检查状态字是否达到 0x0231
                next_step = 2;
            break;
        case 12:
            if ((*status & 0x0233) == 0x0233)   // 检查状态字是否达到 0x0233
                next_step = 3;
            break;
        case 13:
            if ((*status & 0x0237) == 0x0237) { // 检查状态字是否达到 0x0237（使能成功）
                printf("Operation enabled.\n");
                next_step = 4;                  // 进入运动阶段
            }
            break;

        /* ---- 运动循环：0 ↔ 500000 ---- */
        case 4:
            *target = target_positions[s->pos_index];
            *ctrl = 0x000F;          // 清除 bit4（新位置点触发位）
            next_step = 5;
            s->wait_cycles = 0;
            break;

        case 5:
            // 等待 bit12（新位置应答）清除，表示可接收新位置
            if ((*status & 0x1000) == 0x0000) {
                next_step = 6;
            } else {
                s->wait_cycles++;
                if (s->wait_cycles > 200) {   // 超时重试（200ms）
                    printf("bit12 timeout, retry\n");
                    next_step = 4;
                }
            }
            break;

        case 6:
            *ctrl = 0x001F;          // 置位 bit4，产生上升沿，触发新位置运动
            next_step = 7;
            s->wait_cycles = 0;
            break;

        case 7:
            // 等待 bit12 置 1，表示运动开始执行
            if ((*status & 0x1000) != 0x0000) {
                next_step = 8;
            } else {
                s->wait_cycles++;
                if (s->wait_cycles > 200) {
                    printf("motion start timeout, retry\n");
                    next_step = 4;
                }
            }
            break;

        case 8:
            // 等待目标到达（bit10=1）
            if (*status & 0x0400) {
                *ctrl = 0x000F;          // 到达后立即清除 bit4，为下一次触发准备
                next_step = 9;
                s->wait_cycles = 0;
            }
            break;

        case 9:
            // 等待 bit12 清除（确认上一次触发已完成），然后切换目标
            if ((*status & 0x1000) == 0x0000) {
                s->pos_index = (s->pos_index + 1) % 2;  // 切换目标 0↔500000
                next_step = 4;
            } else {
                s->wait_cycles++;
                if (s->wait_cycles > 200) {
                    printf("bit12 clear timeout, retry\n");
                    s->pos_index = (s->pos_index + 1) % 2;
                    next_step = 4;
                }
            }
            break;

        default: break;
    }
    return next_step;
}

/* ===== 主函数 ===== */
int main() {
    /* ---- 1. 实时性增强 ---- */
    // 锁定所有内存页，防止被交换到磁盘（避免缺页异常）
    if (mlockall(MCL_CURRENT | MCL_FUTURE) == -1)
        perror("mlockall");

    // 设置主线程为 SCHED_FIFO 实时调度，优先级 80（较高）
    struct sched_param param = { .sched_priority = 80 };
    if (pthread_setschedparam(pthread_self(), SCHED_FIFO, &param) != 0)
        perror("setschedparam");

    /* ---- 2. 请求主站实例 ---- */
    master = ecrt_request_master(0);
    if (!master) {
        fprintf(stderr, "Failed to request master 0\n");
        return 1;
    }

    /* ---- 3. 创建过程数据域 ---- */
    domain = ecrt_master_create_domain(master);
    if (!domain) {
        fprintf(stderr, "Failed to create domain\n");
        return 1;
    }

    /* ---- 4. 获取从站配置 ---- */
    sc = ecrt_master_slave_config(master, 0, 0, VENDOR_ID, PRODUCT_CODE);
    if (!sc) {
        fprintf(stderr, "Failed to get slave config\n");
        return 1;
    }

    /* ---- 5. 注册 PDO entry，获取偏移量 ---- */
    /*
     * 使用 ecrt_slave_config_reg_pdo_entry 获取每个 PDO 对象在 domain 中的偏移。
     * 该函数返回偏移值（>=0），若返回负值表示注册失败。
     * 最后一个参数 NULL 表示不填充指针，而是直接返回值作为偏移。
     * 这种方式更可靠，避免版本兼容问题。
     */
    int ret;
    ret = ecrt_slave_config_reg_pdo_entry(sc, 0x6040, 0x00, domain, NULL);
    if (ret < 0) { fprintf(stderr, "reg 0x6040 failed, ret=%d\n", ret); return 1; }
    off_ctrl = ret;

    ret = ecrt_slave_config_reg_pdo_entry(sc, 0x607A, 0x00, domain, NULL);
    if (ret < 0) { fprintf(stderr, "reg 0x607A failed, ret=%d\n", ret); return 1; }
    off_target = ret;

    ret = ecrt_slave_config_reg_pdo_entry(sc, 0x6041, 0x00, domain, NULL);
    if (ret < 0) { fprintf(stderr, "reg 0x6041 failed, ret=%d\n", ret); return 1; }
    off_status = ret;

    ret = ecrt_slave_config_reg_pdo_entry(sc, 0x6064, 0x00, domain, NULL);
    if (ret < 0) { fprintf(stderr, "reg 0x6064 failed, ret=%d\n", ret); return 1; }
    off_pos = ret;

    /* ---- 6. 激活主站，进入 OP 状态 ---- */
    if (ecrt_master_activate(master)) {
        fprintf(stderr, "Failed to activate master\n");
        return 1;
    }

    /* ---- 7. 获取 domain 数据指针 ---- */
    uint8_t *domain_pd = ecrt_domain_data(domain);
    if (!domain_pd) {
        fprintf(stderr, "Failed to get domain data\n");
        return 1;
    }

    /* 打印偏移量（用于调试和确认映射） */
    printf("Offsets: ctrl=%u, target=%u, status=%u, pos=%u\n",
           off_ctrl, off_target, off_status, off_pos);

    /* ---- 8. 注册信号处理 ---- */
    signal(SIGINT, signal_handler);

    /* ---- 9. 初始化状态机 ---- */
    init_slave_state();

    /* ---- 10. 准备高精度定时（绝对时间基准） ---- */
    struct timespec ts = {0, 0};
    clock_gettime(CLOCK_REALTIME, &ts);
    printf("Starting cyclic operation (1ms) with 1 slave...\n");

    /* ---- 11. 进入 1ms 周期主循环 ---- */
    struct timespec prev_ts, curr_ts;
    clock_gettime(CLOCK_MONOTONIC, &prev_ts);

    while (running) {
        /* 等待下一个 1ms 周期（绝对时间等待，避免累积误差） */
        ts.tv_nsec += PERIOD_NS;
        if (ts.tv_nsec >= 1000000000) {
            ts.tv_sec++;
            ts.tv_nsec -= 1000000000;
        }
        clock_nanosleep(CLOCK_REALTIME, TIMER_ABSTIME, &ts, NULL);

        /* ---- 抖动测量 ---- */
        clock_gettime(CLOCK_MONOTONIC, &curr_ts);
        long long cycle_ns = (curr_ts.tv_sec - prev_ts.tv_sec) * 1000000000LL +
                             (curr_ts.tv_nsec - prev_ts.tv_nsec);
        long long dev_ns = cycle_ns - PERIOD_NS;

        /* 更新统计 */
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

        /* 每 10000 个周期输出一次实时抖动概览 */
        if (stats.count % 10000 == 0) {
            double avg_dev = stats.sum_dev / stats.count;
            printf("[JITTER] samples=%lld, avg_dev=%.2f ns, max_dev=%lld ns\n",
                   stats.count, avg_dev, stats.max_dev);
        }

        prev_ts = curr_ts;

        /* ---- EtherCAT 通信 ---- */
        ecrt_master_receive(master);      // 接收所有从站的帧
        ecrt_domain_process(domain);      // 处理 domain 数据

        /* ---- 运行状态机 ---- */
        uint16_t *status = (uint16_t *)(domain_pd + off_status);
        uint16_t *ctrl   = (uint16_t *)(domain_pd + off_ctrl);
        int32_t  *target = (int32_t  *)(domain_pd + off_target);
        int32_t  *pos    = (int32_t  *)(domain_pd + off_pos);

        slave_state.step = run_slave_fsm(status, ctrl, target, pos);

        /* ---- 发送帧 ---- */
        ecrt_domain_queue(domain);        // 排队 domain 数据
        ecrt_master_send(master);         // 发送所有排队帧
    }

    /* ---- 退出：打印最终统计 ---- */
    print_jitter_report();

    /* ---- 释放主站 ---- */
    ecrt_release_master(master);
    return 0;
}