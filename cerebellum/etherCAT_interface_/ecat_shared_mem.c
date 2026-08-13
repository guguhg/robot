/*
 * ====================================================================
 * 文件名: ecat_shared_mem.c
 * 功能: 共享内存实现
 * ====================================================================
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <errno.h>
#include <time.h>

#include "ecat_shared_mem.h"

ecat_shared_data_t* ecat_shm_create(void) {
    int fd = shm_open(ECAT_SHM_NAME, O_CREAT | O_RDWR, 0666);
    if (fd < 0) {
        perror("shm_open create");
        return NULL;
    }

    if (ftruncate(fd, ECAT_SHM_SIZE) < 0) {
        perror("ftruncate");
        close(fd);
        return NULL;
    }

    void *map = mmap(NULL, ECAT_SHM_SIZE, PROT_READ | PROT_WRITE,
                     MAP_SHARED, fd, 0);
    close(fd);

    if (map == MAP_FAILED) {
        perror("mmap");
        return NULL;
    }

    // 初始化
    ecat_shared_data_t *shm = (ecat_shared_data_t*)map;
    memset(shm, 0, ECAT_SHM_SIZE);
    shm->magic = ECAT_MAGIC;
    shm->version = 3;       // v3.0 双缓冲
    shm->num_axes = 2;  // 可改为动态参数

    printf("[SharedMem] Created at %p, axes=%d\n", shm, shm->num_axes);
    return shm;
}

ecat_shared_data_t* ecat_shm_open_existing(void) {
    int fd = shm_open(ECAT_SHM_NAME, O_RDWR, 0666);
    if (fd < 0) {
        perror("shm_open open");
        return NULL;
    }

    void *map = mmap(NULL, ECAT_SHM_SIZE, PROT_READ | PROT_WRITE,
                     MAP_SHARED, fd, 0);
    close(fd);

    if (map == MAP_FAILED) {
        perror("mmap");
        return NULL;
    }

    ecat_shared_data_t *shm = (ecat_shared_data_t*)map;
    if (shm->magic != ECAT_MAGIC) {
        fprintf(stderr, "[SharedMem] Invalid magic: 0x%08X\n", shm->magic);
        munmap(shm, ECAT_SHM_SIZE);
        return NULL;
    }

    printf("[SharedMem] Opened at %p, axes=%d\n", shm, shm->num_axes);
    return shm;
}

void ecat_shm_close(ecat_shared_data_t *shm) {
    if (shm) {
        munmap(shm, ECAT_SHM_SIZE);
        shm_unlink(ECAT_SHM_NAME);
        printf("[SharedMem] Closed\n");
    }
}

bool ecat_shm_wait_ready(ecat_shared_data_t *shm, int timeout_ms) {
    struct timespec start, now;
    clock_gettime(CLOCK_MONOTONIC, &start);

    while (1) {
        if (shm->flags & ECAT_FLAG_READY) {
            return true;
        }
        if (timeout_ms > 0) {
            clock_gettime(CLOCK_MONOTONIC, &now);
            long elapsed = (now.tv_sec - start.tv_sec) * 1000 +
                           (now.tv_nsec - start.tv_nsec) / 1000000;
            if (elapsed >= timeout_ms) {
                return false;
            }
        }
        usleep(1000);
    }
}

/* ===== 双缓冲读写 API (v3.0) ===== */

ecat_axis_data_t* ecat_shm_writer_begin(ecat_shared_data_t *shm) {
    uint32_t back = !shm->write_idx;
    return shm->axes[back];
}

void ecat_shm_writer_commit(ecat_shared_data_t *shm) {
    uint32_t back = !shm->write_idx;
    __sync_synchronize();          // 确保后台写入全部完成，内存屏障——告诉编译器和 CPU：我这行上面的所有内存写入，必须全部完成并全局可见之后，才能执行下面的代码。
    shm->write_idx = back;         // 翻转: 让读者看到新数据
    shm->seq++;
    shm->cycle_count++;
}

ecat_axis_data_t* ecat_shm_reader_begin(ecat_shared_data_t *shm, uint32_t *seq) {
    *seq = shm->seq;
    __sync_synchronize();
    uint32_t front = shm->write_idx;
    return shm->axes[front];
}

bool ecat_shm_reader_end(ecat_shared_data_t *shm, uint32_t saved_seq) {
    __sync_synchronize();
    return (shm->seq == saved_seq);
}