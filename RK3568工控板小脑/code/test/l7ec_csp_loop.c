/*
 * ====================================================================
 * 文件名: l7ec_csp_loop.c 循环同步位置模式
 * 功能: 单雷赛 L7EC-400S CSP 模式连续运动 + 实时抖动统计
 *       控制电机以固定速度连续旋转（三角波往复），测量 1ms 周期抖动。
 *
 * 操作模式: CSP (0x6060 = 8)
 * 控制逻辑: 主站每周期更新目标位置 (0x607A)，实现连续轨迹。
 *           位置在 0~500000 之间线性变化，到达边界后反向。
 *
 * 注意: CSP 模式建议配合 DC 同步时钟，但自由运行也可工作（取决于驱动器）。
 *       如果实际运动不平滑，请检查主站周期抖动是否过大。
 *
 * 编译: gcc -o l7ec_csp_loop l7ec_csp_loop.c \
 *           -I/opt/etherlab/include -L/opt/etherlab/lib \
 *           -lethercat -lpthread -lrt -lm
 * 运行: sudo LD_LIBRARY_PATH=/opt/etherlab/lib ./l7ec_csp_loop
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

#define PERIOD_NS 1000000
#define VENDOR_ID  0x00004321
#define PRODUCT_CODE 0x000000b2

static ec_master_t *master = NULL;
static ec_domain_t *domain = NULL;
static ec_slave_config_t *sc = NULL;

static unsigned int off_ctrl, off_target, off_status, off_pos;

/* 使用默认 PDO 映射（与 PP 相同，包含 0x6040, 0x607A, 0x6041, 0x6064） */
static ec_pdo_entry_reg_t pdo_entries[] = {
    {0, 0, VENDOR_ID, PRODUCT_CODE, 0x6040, 0, &off_ctrl},
    {0, 0, VENDOR_ID, PRODUCT_CODE, 0x607A, 0, &off_target},
    {0, 0, VENDOR_ID, PRODUCT_CODE, 0x6041, 0, &off_status},
    {0, 0, VENDOR_ID, PRODUCT_CODE, 0x6064, 0, &off_pos},
    {}
};

volatile sig_atomic_t running = 1;

typedef struct {
    long long count;
    double sum_cycle;
    double sum_dev;
    double sum_sq_dev;
    long long min_dev;
    long long max_dev;
} jitter_stats_t;

static jitter_stats_t stats = {0, 0.0, 0.0, 0.0, LLONG_MAX, LLONG_MIN};

typedef struct {
    int step;
    int wait_cycles;
    int print_count;
} slave_state_t;

static slave_state_t slave_state;

/* 运动参数：位置增量（每周期增加 1000 指令单位，1ms 周期 = 1,000,000 单位/秒） */
#define POS_STEP 1000
#define POS_MIN  0
#define POS_MAX  500000

/* 信号处理 */
void signal_handler(int sig) {
    (void)sig;
    running = 0;
}

/* 打印统计 */
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

void init_slave_state(void) {
    slave_state.step = 0;
    slave_state.wait_cycles = 0;
    slave_state.print_count = 0;
}

/*
 * 状态机：使能流程与 PP 相同，但使能后进入 CSP 运动循环。
 */
int run_slave_fsm(uint16_t *status, uint16_t *ctrl, int32_t *target, int32_t *pos) {
    slave_state_t *s = &slave_state;
    int next_step = s->step;

    switch (s->step) {
        case 0:
            *ctrl = 0x0000; next_step = 10; break;
        case 1:
            *ctrl = 0x0006; next_step = 11; break;
        case 2:
            *ctrl = 0x0007; next_step = 12; break;
        case 3:
            *ctrl = 0x000F; next_step = 13; break;

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
                printf("Operation enabled (CSP).\n");
                next_step = 4;
            }
            break;

        /* CSP 运动循环：连续更新目标位置（三角波） */
        case 4:
            // 每周期更新目标位置，由主站持续规划
            // 控制字保持 0x000F（使能状态），无需触发位
            *ctrl = 0x000F;
            // 目标位置在 POS_MIN ~ POS_MAX 之间线性变化
            static int32_t current_pos = 0;
            static int32_t direction = 1;  // 1 递增，-1 递减

            current_pos += direction * POS_STEP;
            if (current_pos > POS_MAX) {
                current_pos = POS_MAX;
                direction = -1;
            } else if (current_pos < POS_MIN) {
                current_pos = POS_MIN;
                direction = 1;
            }
            *target = current_pos;
            // 保持步骤不变，循环执行
            break;

        default: break;
    }
    return next_step;
}

int main() {
    if (mlockall(MCL_CURRENT | MCL_FUTURE) == -1) perror("mlockall");

    struct sched_param param = { .sched_priority = 80 };
    if (pthread_setschedparam(pthread_self(), SCHED_FIFO, &param) != 0) perror("setschedparam");

    master = ecrt_request_master(0);
    if (!master) { fprintf(stderr, "Failed to request master 0\n"); return 1; }

    domain = ecrt_master_create_domain(master);
    if (!domain) { fprintf(stderr, "Failed to create domain\n"); return 1; }

    sc = ecrt_master_slave_config(master, 0, 0, VENDOR_ID, PRODUCT_CODE);
    if (!sc) { fprintf(stderr, "Failed to get slave config\n"); return 1; }

    /* 使用默认 PDO 映射（与 PP 相同） */
    if (ecrt_domain_reg_pdo_entry_list(domain, pdo_entries)) {
        fprintf(stderr, "Failed to register PDO entries\n");
        return 1;
    }

    if (ecrt_master_activate(master)) {
        fprintf(stderr, "Failed to activate master\n");
        return 1;
    }

    uint8_t *domain_pd = ecrt_domain_data(domain);
    if (!domain_pd) {
        fprintf(stderr, "Failed to get domain data\n");
        return 1;
    }

    printf("Offsets: ctrl=%u, target=%u, status=%u, pos=%u\n",
           off_ctrl, off_target, off_status, off_pos);

    signal(SIGINT, signal_handler);
    init_slave_state();

    struct timespec ts = {0, 0};
    clock_gettime(CLOCK_REALTIME, &ts);
    printf("Starting CSP cyclic operation (1ms) ...\n");

    struct timespec prev_ts, curr_ts;
    clock_gettime(CLOCK_MONOTONIC, &prev_ts);

    while (running) {
        ts.tv_nsec += PERIOD_NS;
        if (ts.tv_nsec >= 1000000000) { ts.tv_sec++; ts.tv_nsec -= 1000000000; }
        clock_nanosleep(CLOCK_REALTIME, TIMER_ABSTIME, &ts, NULL);

        clock_gettime(CLOCK_MONOTONIC, &curr_ts);
        long long cycle_ns = (curr_ts.tv_sec - prev_ts.tv_sec) * 1000000000LL +
                             (curr_ts.tv_nsec - prev_ts.tv_nsec);
        long long dev_ns = cycle_ns - PERIOD_NS;

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

        ecrt_master_receive(master);
        ecrt_domain_process(domain);

        uint16_t *status = (uint16_t *)(domain_pd + off_status);
        uint16_t *ctrl   = (uint16_t *)(domain_pd + off_ctrl);
        int32_t  *target = (int32_t  *)(domain_pd + off_target);
        int32_t  *pos    = (int32_t  *)(domain_pd + off_pos);

        slave_state.step = run_slave_fsm(status, ctrl, target, pos);

        ecrt_domain_queue(domain);
        ecrt_master_send(master);
    }

    print_jitter_report();
    ecrt_release_master(master);
    return 0;
}