/**
 * 共享内存读取端 — 读取车辆传感器数据
 *
 * 编译: gcc -o shm_reader shm_reader.c -lrt -lpthread
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
#include <stdint.h>

/* ============================================================
 * 配置 (必须与 writer 完全一致)
 * ============================================================ */
#define SHM_NAME   "/vehicle_sensor_shm"
#define SHM_SIZE   4096

static volatile int running = 1;

/* ============================================================
 * 共享内存数据结构 (必须与 writer 完全一致)
 * ============================================================ */
typedef struct {
    uint64_t sequence;
    uint64_t timestamp_us;
    double   speed;
    double   temperature;
    double   latitude;
    double   longitude;
    double   battery_soc;
    int32_t  status;
    char     vehicle_id[32];
    char     padding[36];
} vehicle_sensor_t;

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
    vehicle_sensor_t *shm_ptr;

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    /* 1. 打开共享内存 (只读也 OK, 但这里用读写方便) */
    fd = shm_open(SHM_NAME, O_RDWR, 0666);
    if (fd == -1) {
        perror("[ERR] shm_open — 请先启动 shm_writer");
        return 1;
    }

    /* 2. 获取大小 */
    struct stat sb;
    if (fstat(fd, &sb) == -1) {
        perror("[ERR] fstat");
        close(fd);
        return 1;
    }
    printf("[READER] 共享内存大小: %ld bytes\n", (long)sb.st_size);

    /* 3. 映射 */
    shm_ptr = (vehicle_sensor_t *)mmap(NULL, SHM_SIZE,
                                        PROT_READ | PROT_WRITE,
                                        MAP_SHARED, fd, 0);
    if (shm_ptr == MAP_FAILED) {
        perror("[ERR] mmap");
        close(fd);
        return 1;
    }
    close(fd);

    printf("[READER] 已映射共享内存: %s\n", SHM_NAME);
    printf("[READER] 等待传感器数据...\n");
    printf("按 Ctrl+C 退出\n\n");

    /* 4. 轮询读取 */
    const char *status_names[] = {"STOP", "RUN", "CHARGE", "ERROR"};
    uint64_t last_seq = 0;

    while (running) {
        /* 检查是否有新数据 (通过 sequence 字段) */
        if (shm_ptr->sequence != last_seq) {
            last_seq = shm_ptr->sequence;

            /* 计算延迟 */
            struct timespec ts;
            clock_gettime(CLOCK_REALTIME, &ts);
            uint64_t now_us = (uint64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
            double latency_us = (double)(now_us - shm_ptr->timestamp_us);

            printf("[READ] #%03lu | id=%s | speed=%.1f km/h | "
                   "temp=%.1f°C | pos=(%.6f,%.6f) | "
                   "battery=%.1f%% | status=%s | 延迟=%.1f us\n",
                   (unsigned long)last_seq,
                   shm_ptr->vehicle_id,
                   shm_ptr->speed,
                   shm_ptr->temperature,
                   shm_ptr->latitude,
                   shm_ptr->longitude,
                   shm_ptr->battery_soc,
                   (shm_ptr->status >= 0 && shm_ptr->status <= 3)
                       ? status_names[shm_ptr->status] : "?",
                   latency_us);

            /* 故障告警 */
            if (shm_ptr->status == 3) {
                printf("[READ] ⚠ 告警: 车辆故障! speed=%.1f\n", shm_ptr->speed);
            }
        }

        usleep(100000);  /* 100ms 轮询间隔 */
    }

    printf("\n[READER] 停止读取\n");
    munmap(shm_ptr, SHM_SIZE);
    return 0;
}
