/**
 * 共享内存生产者 — 信号量保护的环形缓冲区写入
 *
 * 使用 POSIX 命名信号量实现生产者-消费者同步
 *
 * 编译: gcc -o shm_sem_producer shm_sem_producer.c -lrt -lpthread
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <time.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <semaphore.h>
#include <stdint.h>

#include "ringbuf.h"

/* ============================================================
 * 配置
 * ============================================================ */
#define SHM_NAME           "/robot_cmd_shm"
#define SEM_EMPTY_NAME     "/robot_cmd_empty"   /* 空闲槽位数 */
#define SEM_FULL_NAME      "/robot_cmd_full"    /* 已填充槽位数 */
#define SEM_MUTEX_NAME     "/robot_cmd_mutex"   /* 互斥锁 (多生产者时用) */

#define RINGBUF_CAPACITY   64
#define ELEM_SIZE          64    /* 每条命令最大64字节 */

/* 共享内存总大小 */
#define SHM_TOTAL_SIZE  ringbuf_total_size(RINGBUF_CAPACITY, ELEM_SIZE)

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

    srand((unsigned int)time(NULL));
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    /* ======== 创建/打开共享内存 ======== */
    fd = shm_open(SHM_NAME, O_CREAT | O_RDWR | O_TRUNC, 0666);
    if (fd == -1) { perror("[ERR] shm_open"); return 1; }

    if (ftruncate(fd, SHM_TOTAL_SIZE) == -1) {
        perror("[ERR] ftruncate");
        close(fd);
        shm_unlink(SHM_NAME);
        return 1;
    }

    ctrl = (ringbuf_ctrl_t *)mmap(NULL, SHM_TOTAL_SIZE,
                                   PROT_READ | PROT_WRITE,
                                   MAP_SHARED, fd, 0);
    if (ctrl == MAP_FAILED) { perror("[ERR] mmap"); close(fd); return 1; }
    close(fd);

    /* 初始化环形缓冲区 (仅首次创建时) */
    ringbuf_init(ctrl, RINGBUF_CAPACITY, ELEM_SIZE);

    /* ======== 创建/打开信号量 ======== */
    sem_empty = sem_open(SEM_EMPTY_NAME, O_CREAT, 0666, RINGBUF_CAPACITY);
    sem_full  = sem_open(SEM_FULL_NAME,  O_CREAT, 0666, 0);

    if (sem_empty == SEM_FAILED || sem_full == SEM_FAILED) {
        perror("[ERR] sem_open");
        munmap(ctrl, SHM_TOTAL_SIZE);
        shm_unlink(SHM_NAME);
        return 1;
    }

    printf("[PRODUCER] 共享内存环形缓冲区已创建\n");
    printf("[PRODUCER] 容量: %lu 条命令, 每条 %lu bytes\n",
           (unsigned long)RINGBUF_CAPACITY, (unsigned long)ELEM_SIZE);
    printf("[PRODUCER] 开始写入机器人控制命令 (1Hz)...\n");
    printf("按 Ctrl+C 退出\n\n");

    /* ======== 生产循环 ======== */
    const char *commands[] = {
        "MOVE_FORWARD 1.0",
        "TURN_LEFT 0.5",
        "MOVE_FORWARD 0.5",
        "TURN_RIGHT 0.3",
        "STOP",
        "MOVE_BACKWARD 0.2",
        "ARM_LIFT 45",
        "GRIPPER_CLOSE",
    };
    int cmd_count = sizeof(commands) / sizeof(commands[0]);
    int seq = 0;

    while (running) {
        /* 等待空闲槽位 */
        if (sem_wait(sem_empty) == -1) break;

        /* 写入数据 */
        const char *cmd = commands[seq % cmd_count];
        char elem[ELEM_SIZE];
        snprintf(elem, sizeof(elem), "#%d:%s", seq, cmd);

        if (ringbuf_write(ctrl, elem) != 0) {
            fprintf(stderr, "[ERR] ringbuf_write 失败\n");
            sem_post(sem_empty);  /* 归还槽位 */
            break;
        }

        /* 通知消费者有新数据 */
        sem_post(sem_full);

        printf("[PRODUCE] #%03d | %s | "
               "缓冲区: %lu/%lu\n",
               seq, cmd,
               (unsigned long)ringbuf_count(ctrl),
               (unsigned long)RINGBUF_CAPACITY);

        seq++;
        sleep(1);
    }

    /* ======== 清理 ======== */
    printf("\n[PRODUCER] 停止生产\n");

    munmap(ctrl, SHM_TOTAL_SIZE);
    /* 注意: 生产者不删除共享内存和信号量, 留给消费者处理 */
    sem_close(sem_empty);
    sem_close(sem_full);

    return 0;
}
