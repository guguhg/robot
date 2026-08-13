/*
 * ====================================================================
 * 文件名: ros2_control_test.c
 * 功能: 模拟 ros2_control 硬件组件的测试程序
 * 
 * 演示:
 *   - 生命周期管理：init → configure → activate → read/write → deactivate → cleanup
 *   - 通过共享内存与 EtherCAT 进程交换数据
 *   - 交互式命令：用户可动态修改目标位置、速度等
 * 
 * 编译:
 *   gcc -o ros2_control_test ros2_control_test.c ecat_shared_mem.c \
 *       -lpthread -lrt -lm
 * 
 * 运行:
 *   sudo ./ros2_control_test
 * 
 * 交互命令:
 *   pos0 <value>   - 设置从站 0 目标位置
 *   pos1 <value>   - 设置从站 1 目标位置
 *   vel0 <value>   - 设置从站 0 目标速度（预留）
 *   enable         - 设置控制字为使能 (0x000F)
 *   disable        - 设置控制字为禁用 (0x0000)
 *   status         - 打印当前所有轴状态
 *   quit           - 退出程序
 * ====================================================================
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include "ecat_shared_mem.h"

/* ===== 生命周期状态 ===== */
typedef enum {
    STATE_UNINITIALIZED = 0,
    STATE_INITIALIZED,
    STATE_CONFIGURED,
    STATE_ACTIVATED,
    STATE_INACTIVE,
    STATE_ERROR
} hw_state_t;

/* ===== 全局变量 ===== */
static ecat_shared_data_t *shm = NULL;
static hw_state_t hw_state = STATE_UNINITIALIZED;
static volatile sig_atomic_t running = 1;

/* ===== 信号处理 ===== */
void signal_handler(int sig) {
    (void)sig;
    running = 0;
}

/* ===== 生命周期函数 ===== */

/**
 * 初始化阶段：分配资源，但不连接硬件
 */
int hw_init(void) {
    printf("[HW] Initializing...\n");
    if (hw_state != STATE_UNINITIALIZED) {
        fprintf(stderr, "[HW] Error: Already initialized\n");
        return -1;
    }
    // 可以在这里分配内存、初始化数据结构等
    // 此处只是演示
    hw_state = STATE_INITIALIZED;
    printf("[HW] Initialization complete.\n");
    return 0;
}

/**
 * 配置阶段：连接共享内存，验证数据
 */
int hw_configure(void) {
    printf("[HW] Configuring...\n");
    if (hw_state != STATE_INITIALIZED) {
        fprintf(stderr, "[HW] Error: Not initialized\n");
        return -1;
    }

    // 打开共享内存（由 EtherCAT 进程创建）
    shm = ecat_shm_open_existing();
    if (!shm) {
        fprintf(stderr, "[HW] Failed to open shared memory\n");
        hw_state = STATE_ERROR;
        return -1;
    }

    // 等待 EtherCAT 就绪（最多 5 秒）
    if (!ecat_shm_wait_ready(shm, 5000)) {
        fprintf(stderr, "[HW] Timeout waiting for EtherCAT ready\n");
        ecat_shm_close(shm);
        shm = NULL;
        hw_state = STATE_ERROR;
        return -1;
    }

    hw_state = STATE_CONFIGURED;
    printf("[HW] Configuration complete. Shared memory connected, axes=%d\n", shm->num_axes);
    return 0;
}

/**
 * 激活阶段：准备开始数据交换
 */
int hw_activate(void) {
    printf("[HW] Activating...\n");
    if (hw_state != STATE_CONFIGURED) {
        fprintf(stderr, "[HW] Error: Not configured\n");
        return -1;
    }

    // 双缓冲: 写入后台后 commit
    {
        ecat_axis_data_t *ax = ecat_shm_writer_begin(shm);
        for (int i = 0; i < shm->num_axes; i++) {
            ax[i].control_word = 0x000F;
            ax[i].target_position = 0;
            ax[i].target_velocity = 0;
            ax[i].target_torque = 0;
        }
        ecat_shm_writer_commit(shm);
    }

    hw_state = STATE_ACTIVATED;
    printf("[HW] Activation complete.\n");
    return 0;
}

/**
 * 读数据：从共享内存读取状态
 */
int hw_read(void) {
    if (hw_state != STATE_ACTIVATED) {
        fprintf(stderr, "[HW] Error: Not activated\n");
        return -1;
    }

    uint32_t seq;
    ecat_axis_data_t *ax = ecat_shm_reader_begin(shm, &seq);
    for (int i = 0; i < shm->num_axes; i++) {
        // 读取 ax[i].actual_position / status_word 等到本地缓存
        (void)ax[i].actual_position;
    }
    ecat_shm_reader_end(shm, seq);
    return 0;
}

/**
 * 写数据：将命令写入共享内存
 */
int hw_write(void) {
    if (hw_state != STATE_ACTIVATED) {
        fprintf(stderr, "[HW] Error: Not activated\n");
        return -1;
    }

    // 命令已在外部通过交互修改（直接操作共享内存变量shm）
    // 此函数可留空，或执行额外处理
    return 0;
}

/**
 * 停用阶段：停止数据交换
 */
int hw_deactivate(void) {
    printf("[HW] Deactivating...\n");
    if (hw_state != STATE_ACTIVATED) {
        fprintf(stderr, "[HW] Error: Not activated\n");
        return -1;
    }

    // 双缓冲: 清除使能
    {
        ecat_axis_data_t *ax = ecat_shm_writer_begin(shm);
        for (int i = 0; i < shm->num_axes; i++) ax[i].control_word = 0x0000;
        ecat_shm_writer_commit(shm);
    }

    hw_state = STATE_INACTIVE;
    printf("[HW] Deactivation complete.\n");
    return 0;
}

/**
 * 清理阶段：释放资源
 */
int hw_cleanup(void) {
    printf("[HW] Cleaning up...\n");
    if (hw_state == STATE_ACTIVATED) {
        hw_deactivate();
    }
    if (shm) {
        ecat_shm_close(shm);
        shm = NULL;
    }
    hw_state = STATE_UNINITIALIZED;
    printf("[HW] Cleanup complete.\n");
    return 0;
}

/* ===== 辅助函数：打印轴状态 (双缓冲读取) ===== */
void print_axis_status(int idx) {
    if (!shm || idx >= shm->num_axes) return;
    uint32_t seq;
    ecat_axis_data_t *ax = ecat_shm_reader_begin(shm, &seq);
    // 拷贝到栈上再打印, 避免读期间数据变化
    double tp = ax[idx].target_position, ap = ax[idx].actual_position;
    double av = ax[idx].actual_velocity;
    uint16_t sw = ax[idx].status_word, cw = ax[idx].control_word;
    bool en = ax[idx].is_enabled, tr = ax[idx].target_reached, flt = ax[idx].is_fault;
    ecat_shm_reader_end(shm, seq);

    printf("Axis %d:\n", idx);
    printf("  Target Position: %.2f\n", tp);
    printf("  Actual Position: %.2f\n", ap);
    printf("  Actual Velocity: %.2f\n", av);
    printf("  Status Word: 0x%04X\n", sw);
    printf("  Control Word: 0x%04X\n", cw);
    printf("  Enabled: %s\n", en ? "YES" : "NO");
    printf("  Target Reached: %s\n", tr ? "YES" : "NO");
    printf("  Fault: %s\n", flt ? "YES" : "NO");
}

/* ===== 主交互循环 ===== */
void interactive_loop(void) {
    char cmd[256];
    char arg1[64], arg2[64];
    int axis;
    double value;

    printf("\n========================================\n");
    printf("ROS2 Control Test - Interactive Mode\n");
    printf("Commands:\n");
    printf("  pos0 <value>   - Set axis 0 target position\n");
    printf("  pos1 <value>   - Set axis 1 target position\n");
    printf("  enable         - Set control word to 0x000F (enable)\n");
    printf("  disable        - Set control word to 0x0000 (disable)\n");
    printf("  status         - Print all axis status\n");
    printf("  quit           - Exit\n");
    printf("========================================\n");

    while (running) {
        printf("\n> ");
        fflush(stdout);
        if (!fgets(cmd, sizeof(cmd), stdin)) break;

        // 去掉换行
        cmd[strcspn(cmd, "\n")] = '\0';

        if (strcmp(cmd, "quit") == 0 || strcmp(cmd, "exit") == 0) {
            break;
        } else if (strcmp(cmd, "status") == 0) {
            for (int i = 0; i < shm->num_axes; i++) {
                print_axis_status(i);
            }
        } else if (strcmp(cmd, "enable") == 0) {
            ecat_axis_data_t *ax = ecat_shm_writer_begin(shm);
            for (int i = 0; i < shm->num_axes; i++) ax[i].control_word = 0x000F;
            ecat_shm_writer_commit(shm);
            printf("All axes enabled.\n");
        } else if (strcmp(cmd, "disable") == 0) {
            ecat_axis_data_t *ax = ecat_shm_writer_begin(shm);
            for (int i = 0; i < shm->num_axes; i++) ax[i].control_word = 0x0000;
            ecat_shm_writer_commit(shm);
            printf("All axes disabled.\n");
        } else if (sscanf(cmd, "pos%d %lf", &axis, &value) == 2) {
            if (axis >= 0 && axis < shm->num_axes) {
                ecat_axis_data_t *ax = ecat_shm_writer_begin(shm);
                ax[axis].target_position = value;
                ecat_shm_writer_commit(shm);
                printf("Axis %d target position set to %.2f\n", axis, value);
            } else {
                printf("Invalid axis. Available: 0-%d\n", shm->num_axes - 1);
            }
        } else {
            printf("Unknown command. Available commands:\n");
            printf("  pos0 <val>, pos1 <val>, enable, disable, status, quit\n");
        }
    }
}

/* ===== 主函数 ===== */
int main(int argc, char **argv) {
    printf("=== ROS2 Control Test (Shared Memory IPC) ===\n");

    // 信号处理
    signal(SIGINT, signal_handler);

    // 1. 初始化
    if (hw_init() != 0) {
        fprintf(stderr, "Init failed\n");
        return 1;
    }

    // 2. 配置
    if (hw_configure() != 0) {
        fprintf(stderr, "Configure failed\n");
        hw_cleanup();
        return 1;
    }

    // 3. 激活
    if (hw_activate() != 0) {
        fprintf(stderr, "Activate failed\n");
        hw_cleanup();
        return 1;
    }

    printf("\n=== System ready. Starting interactive control ===\n");

    // 4. 进入交互循环（模拟 read/write 周期）
    //    实际 ros2_control 会在单独的线程中周期性调用 read/write
    //    这里我们在交互循环中模拟：每 100ms 读取一次状态，同时响应用户命令
    struct timespec sleep_time = {0, 100000000};  // 100ms

    // 启动一个简单的更新循环（在用户输入的间隙自动 read/write）
    // 这里使用非阻塞输入检测，但为简化，我们在每个命令后自动 read
    // 实际可以使用 select 或单独线程，此处保持简单

    // 交互循环
    interactive_loop();

    // 5. 停用
    hw_deactivate();

    // 6. 清理
    hw_cleanup();

    printf("=== Test completed ===\n");
    return 0;
}