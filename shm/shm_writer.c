/**
 * 共享内存写入端 — 模拟车辆传感器数据写入
 *
 * 工作流:
 *   1. shm_open  创建/打开共享内存
 *   2. ftruncate 设置大小
 *   3. mmap      映射到进程地址空间
 *   4. 直接写入数据 (无内核拷贝!)
 *   5. munmap + shm_unlink 清理
 *
 * 编译: gcc -o shm_writer shm_writer.c -lrt -lpthread
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
 * 配置
 * ============================================================ */
#define SHM_NAME        "/vehicle_sensor_shm"
#define SHM_SIZE        4096        /* 4KB, 足够存放传感器数据 */
#define PUBLISH_PERIOD  1           /* 1Hz */

static volatile int running = 1;

/* ============================================================
 * 共享内存数据结构
 * ============================================================ */
typedef struct {
    uint64_t sequence;      /* 帧序号 */
    uint64_t timestamp_us;  /* 时间戳 (微秒) */
    double   speed;         /* 车速 (km/h) */
    double   temperature;   /* 发动机温度 (°C) */
    double   latitude;      /* GPS 纬度 */
    double   longitude;     /* GPS 经度 */
    double   battery_soc;   /* 电池电量 (%) */
    int32_t  status;        /* 0=停止, 1=行驶, 2=充电, 3=异常 */
    char     vehicle_id[32];
    char     padding[36];   /* 对齐填充 */
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
 * 生成模拟数据
 * ============================================================ */
static void generate_sensor_data(vehicle_sensor_t *data, uint64_t seq)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);

    data->sequence     = seq;
    data->timestamp_us = (uint64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
    data->speed        = (double)(rand() % 12000) / 100.0;     /* 0 ~ 120 km/h */
    data->temperature  = 70.0 + (double)(rand() % 4000) / 100.0; /* 70 ~ 110 °C */
    data->latitude     = 22.53 + (rand() % 400 - 200) / 10000.0;
    data->longitude    = 113.93 + (rand() % 400 - 200) / 10000.0;
    data->battery_soc  = 85.0 - seq * 0.1 + (rand() % 20 - 10) / 10.0;
    data->status       = (seq > 20 && seq <= 25) ? 3 : 1;  /* 模拟间歇故障 */
    strncpy(data->vehicle_id, "VIN-SHM-001", sizeof(data->vehicle_id) - 1);
}

/* ============================================================
 * 主逻辑
 * ============================================================ */
int main(void)
{
    int fd;
    vehicle_sensor_t *shm_ptr;

    srand((unsigned int)time(NULL));
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    /* 1. 创建共享内存对象 */
    fd = shm_open(SHM_NAME, O_CREAT | O_RDWR | O_TRUNC, 0666);
    if (fd == -1) {
        perror("[ERR] shm_open");
        return 1;
    }

    /* 2. 设置大小 */
    if (ftruncate(fd, SHM_SIZE) == -1) {
        perror("[ERR] ftruncate");
        close(fd);
        shm_unlink(SHM_NAME);
        return 1;
    }

    /* 3. 映射到进程地址空间 */
    shm_ptr = (vehicle_sensor_t *)mmap(NULL, SHM_SIZE,
                                        PROT_READ | PROT_WRITE,
                                        MAP_SHARED, fd, 0);
    if (shm_ptr == MAP_FAILED) {
        perror("[ERR] mmap");
        close(fd);
        shm_unlink(SHM_NAME);
        return 1;
    }

    /* fd 映射后可以关闭 */
    close(fd);

    printf("[WRITER] 共享内存已创建: %s\n", SHM_NAME);
    printf("[WRITER] 大小: %d bytes, 路径: /dev/shm%s\n", SHM_SIZE, SHM_NAME);
    printf("[WRITER] 开始写入传感器数据 (1Hz)...\n");
    printf("按 Ctrl+C 退出\n\n");

    /* 4. 写入循环 */
    uint64_t seq = 0;
    const char *status_names[] = {"STOP", "RUN", "CHARGE", "ERROR"};

    while (running) {
        seq++;

        /* 直接在共享内存中构造数据 (零拷贝!) */
        generate_sensor_data(shm_ptr, seq);

        printf("[WRITE] #%03lu | speed=%.1f km/h | temp=%.1f°C | "
               "pos=(%.6f,%.6f) | battery=%.1f%% | status=%s\n",
               (unsigned long)seq,
               shm_ptr->speed,
               shm_ptr->temperature,
               shm_ptr->latitude,
               shm_ptr->longitude,
               shm_ptr->battery_soc,
               status_names[shm_ptr->status]);

        sleep(PUBLISH_PERIOD);
    }

    /* 5. 清理 */
    printf("\n[WRITER] 停止写入\n");
    munmap(shm_ptr, SHM_SIZE);
    shm_unlink(SHM_NAME);
    printf("[WRITER] 共享内存已释放\n");

    return 0;
}
