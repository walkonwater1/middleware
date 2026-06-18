/**
 * 共享内存消费者 — 信号量保护的环形缓冲区读取
 *
 * 编译: gcc -o shm_sem_consumer shm_sem_consumer.c -lrt -lpthread
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <semaphore.h>
#include <stdint.h>

#include "ringbuf.h"

/* ============================================================
 * 配置 (必须与 producer 完全一致)
 * ============================================================ */
#define SHM_NAME           "/robot_cmd_shm"
#define SEM_EMPTY_NAME     "/robot_cmd_empty"
#define SEM_FULL_NAME      "/robot_cmd_full"

#define RINGBUF_CAPACITY   64
#define ELEM_SIZE          64
#define SHM_TOTAL_SIZE     ringbuf_total_size(RINGBUF_CAPACITY, ELEM_SIZE)

static volatile int running = 1;

/* ============================================================
 * 信号处理
 * ============================================================ */
static void signal_handler(int sig)
{
    (void)sig;
    running = 0;
}

/* ============================================================
 * 主逻辑
 * ============================================================ */
int main(void)
{
    int fd;
    ringbuf_ctrl_t *ctrl;
    sem_t *sem_empty;
    sem_t *sem_full;

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    /* ======== 打开共享内存 ======== */
    fd = shm_open(SHM_NAME, O_RDWR, 0666);
    if (fd == -1) {
        perror("[ERR] shm_open — 请先启动 shm_sem_producer");
        return 1;
    }

    ctrl = (ringbuf_ctrl_t *)mmap(NULL, SHM_TOTAL_SIZE,
                                   PROT_READ | PROT_WRITE,
                                   MAP_SHARED, fd, 0);
    if (ctrl == MAP_FAILED) { perror("[ERR] mmap"); close(fd); return 1; }
    close(fd);

    /* ======== 打开信号量 ======== */
    sem_empty = sem_open(SEM_EMPTY_NAME, 0);
    sem_full  = sem_open(SEM_FULL_NAME,  0);

    if (sem_empty == SEM_FAILED || sem_full == SEM_FAILED) {
        perror("[ERR] sem_open");
        munmap(ctrl, SHM_TOTAL_SIZE);
        return 1;
    }

    printf("[CONSUMER] 已打开共享内存环形缓冲区\n");
    printf("[CONSUMER] 等待机器人控制命令...\n");
    printf("按 Ctrl+C 退出\n\n");

    /* ======== 消费循环 ======== */
    int count = 0;

    while (running) {
        /* 等待有数据 (带超时, 1秒) */
        struct timespec timeout;
        clock_gettime(CLOCK_REALTIME, &timeout);
        timeout.tv_sec += 1;

        if (sem_timedwait(sem_full, &timeout) == -1) {
            continue;  /* 超时, 继续等待 */
        }

        /* 读取数据 */
        char elem[ELEM_SIZE];
        if (ringbuf_read(ctrl, elem) != 0) {
            fprintf(stderr, "[ERR] ringbuf_read 失败\n");
            sem_post(sem_full);  /* 不应该发生 */
            break;
        }

        /* 通知生产者有空闲槽位 */
        sem_post(sem_empty);

        count++;
        printf("[CONSUME] #%03d | %s | "
               "缓冲区: %lu/%lu\n",
               count, elem,
               (unsigned long)ringbuf_count(ctrl),
               (unsigned long)RINGBUF_CAPACITY);
    }

    /* ======== 清理 ======== */
    printf("\n[CONSUMER] 停止消费 (共处理 %d 条命令)\n", count);

    sem_close(sem_empty);
    sem_close(sem_full);

    munmap(ctrl, SHM_TOTAL_SIZE);

    /* 消费者负责清理共享内存和信号量 */
    shm_unlink(SHM_NAME);
    sem_unlink(SEM_EMPTY_NAME);
    sem_unlink(SEM_FULL_NAME);
    printf("[CONSUMER] 共享内存和信号量已释放\n");

    return 0;
}
