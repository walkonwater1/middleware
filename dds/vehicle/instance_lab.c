/**
 * instance_lab.c — DDS Key 字段 + Instance 管理实验
 *
 * @key 将结构体字段标记为实例标识。每个不同 key 值创建独立的 Instance,
 * 拥有独立生命周期: ALIVE → NOT_ALIVE_DISPOSED → NOT_ALIVE_NO_WRITERS
 *
 * 用法:
 *   终端1: ./instance_lab sub    ← 观察实例生命周期
 *   终端2: ./instance_lab pub    ← 发布 3 辆车, 演示 dispose/unregister
 *
 * 工程意义:
 *   - 多机器人: 每台机器人一个 Instance, 某台掉线时自动通知
 *   - 传感器阵列: 每个传感器独立管理, 故障传感器自动标记 DISPOSED
 *   - 车队管理: 车辆离开覆盖范围时 unregister, 释放资源
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <time.h>
#include <stdbool.h>

#include "dds/dds.h"
#include "vehicle_instance.h"

#define DOMAIN_ID   0
#define TOPIC_NAME  "InstanceLab"

static volatile int running = 1;
static void sig_handler(int s) { (void)s; running = 0; }

static const char* instance_state_name(dds_instance_state_t s) {
    switch (s) {
    case DDS_ALIVE_INSTANCE_STATE:        return "ALIVE ✓";
    case DDS_NOT_ALIVE_DISPOSED_INSTANCE_STATE: return "DISPOSED ✗";
    case DDS_NOT_ALIVE_NO_WRITERS_INSTANCE_STATE: return "NO_WRITERS ○";
    default: return "?";
    }
}

/* ---- Publisher ---- */
static void run_publisher(void)
{
    dds_entity_t pp = dds_create_participant(DOMAIN_ID, NULL, NULL);
    dds_entity_t tp = dds_create_topic(pp, &vehicle_instance_VehicleState_desc,
                                        TOPIC_NAME, NULL, NULL);

    dds_qos_t *q = dds_create_qos();
    dds_qset_reliability(q, DDS_RELIABILITY_RELIABLE, DDS_SECS(1));
    dds_qset_history(q, DDS_HISTORY_KEEP_LAST, 5);
    dds_entity_t wr = dds_create_writer(pp, tp, q, NULL);
    dds_delete_qos(q);

    /* 3 辆车, 各自独立 Instance */
    const char* car_ids[] = {"car-A", "car-B", "car-C"};
    double speeds[]      = {60.0, 45.0, 80.0};

    /* ★ 注册 3 个 Instance */
    dds_instance_handle_t handles[3];
    for (int i = 0; i < 3; i++) {
        vehicle_instance_VehicleState s;
        memset(&s, 0, sizeof(s));
        strncpy(s.vehicle_id, car_ids[i], 32);

        int rc = dds_register_instance(wr, &handles[i], &s);
        printf("[PUB] ★ register_instance: %s (handle=%ld, rc=%d)\n",
               car_ids[i], (long)handles[i], rc);
    }
    printf("\n");

    /* 阶段 1: 正常发布 3 辆车 */
    printf("[PUB] === 阶段1: 3辆车正常发布 (每辆 3 条) ===\n");
    for (int round = 0; round < 3; round++) {
        for (int i = 0; i < 3; i++) {
            vehicle_instance_VehicleState s;
            memset(&s, 0, sizeof(s));
            strncpy(s.vehicle_id, car_ids[i], 32);
            s.speed = speeds[i] + (rand() % 20 - 10);
            s.battery_soc = 80.0 - round * 5.0;

            struct timespec ts;
            clock_gettime(CLOCK_REALTIME, &ts);
            s.timestamp = (int64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;

            dds_write(wr, &s);
            printf("[PUB] %s speed=%.0f SOC=%.1f%%\n", car_ids[i], s.speed, s.battery_soc);
        }
        sleep(1);
    }
    printf("\n");

    /* 阶段 2: dispose car-B (模拟车辆故障) */
    printf("[PUB] === 阶段2: dispose car-B (模拟故障) ===\n");
    {
        vehicle_instance_VehicleState s;
        memset(&s, 0, sizeof(s));
        strncpy(s.vehicle_id, "car-B", 32);
        s.speed = 0;
        s.battery_soc = 0;

        int rc = dds_dispose(wr, &s);
        printf("[PUB] ★ dispose(car-B) → rc=%d\n", rc);
        printf("[PUB]   car-B Instance 变为 NOT_ALIVE_DISPOSED\n");
        printf("[PUB]   所有订阅者会收到 instance_state=DISPOSED 的通知\n");
    }
    sleep(1);
    printf("\n");

    /* 阶段 3: unregister car-C (模拟车辆离开范围) */
    printf("[PUB] === 阶段3: unregister car-C (模拟离开域) ===\n");
    {
        vehicle_instance_VehicleState s;
        memset(&s, 0, sizeof(s));
        strncpy(s.vehicle_id, "car-C", 32);
        int rc = dds_unregister_instance(wr, &s);
        printf("[PUB] ★ unregister_instance(car-C) → rc=%d\n", rc);
        printf("[PUB]   car-C Instance 变为 NOT_ALIVE_NO_WRITERS\n");
    }
    sleep(1);
    printf("\n");

    /* 阶段 4: car-A 继续正常工作 */
    printf("[PUB] === 阶段4: car-A 继续正常发布 (其他车已消失) ===\n");
    for (int round = 0; round < 3; round++) {
        vehicle_instance_VehicleState s;
        memset(&s, 0, sizeof(s));
        strncpy(s.vehicle_id, "car-A", 32);
        s.speed = 65.0 + (rand() % 10);
        s.battery_soc = 70.0 - round;

        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        s.timestamp = (int64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;

        dds_write(wr, &s);
        printf("[PUB] car-A speed=%.0f SOC=%.1f%% (唯一存活)\n", s.speed, s.battery_soc);
        sleep(1);
    }

    printf("\n[PUB] 演示完成! car-A 存活, car-B 已销毁, car-C 已注销\n");
    dds_delete(wr); dds_delete(tp); dds_delete(pp);
}

/* ---- Subscriber ---- */
static void run_subscriber(void)
{
    dds_entity_t pp = dds_create_participant(DOMAIN_ID, NULL, NULL);
    dds_entity_t tp = dds_create_topic(pp, &vehicle_instance_VehicleState_desc,
                                        TOPIC_NAME, NULL, NULL);

    dds_qos_t *q = dds_create_qos();
    dds_qset_reliability(q, DDS_RELIABILITY_RELIABLE, DDS_SECS(1));
    dds_qset_history(q, DDS_HISTORY_KEEP_LAST, 10);
    dds_entity_t rd = dds_create_reader(pp, tp, q, NULL);
    dds_delete_qos(q);

    printf("[SUB] 订阅者已启动\n");
    printf("[SUB] ★ 等待 publisher 演示 Instance 生命周期...\n\n");

    int count = 0;
    dds_instance_state_t last_state[3] = {0, 0, 0};
    const char* car_ids[] = {"car-A", "car-B", "car-C"};
    time_t start = time(NULL);

    while (running && time(NULL) - start < 25) {
        void* samples[8] = {0};
        dds_sample_info_t infos[8];
        memset(infos, 0, sizeof(infos));

        int n = dds_read(rd, samples, infos, 8, 8);
        for (int i = 0; i < n; i++) {
            vehicle_instance_VehicleState* s = samples[i];
            dds_instance_state_t st = infos[i].instance_state;

            /* 判断是哪辆车 */
            int car_idx = -1;
            for (int c = 0; c < 3; c++) {
                if (strcmp(s->vehicle_id, car_ids[c]) == 0) { car_idx = c; break; }
            }

            /* 状态变化时高亮提示 */
            if (car_idx >= 0 && st != last_state[car_idx]) {
                printf("[SUB] ⚡ %s Instance 状态变化: %s → %s\n",
                       car_ids[car_idx],
                       instance_state_name(last_state[car_idx]),
                       instance_state_name(st));
                last_state[car_idx] = st;
            }

            count++;
            printf("[SUB] #%d | %s speed=%.0f SOC=%.1f%% | instance=%s\n",
                   count, s->vehicle_id, s->speed, s->battery_soc,
                   instance_state_name(st));
        }
        if (n > 0) dds_return_loan(rd, samples, n);
        usleep(200000);
    }

    printf("\n[SUB] 共接收 %d 条样本\n", count);
    printf("[SUB] ★ 关键观察:\n");
    printf("[SUB]   1. car-B 的 instance_state 从 ALIVE 变为 DISPOSED\n");
    printf("[SUB]   2. car-C 的 instance_state 变为 NO_WRITERS\n");
    printf("[SUB]   3. car-A 始终保持 ALIVE\n");
    printf("[SUB]   4. 每个 Instance 独立管理, 互不影响\n");

    dds_delete(rd); dds_delete(tp); dds_delete(pp);
}

int main(int argc, char* argv[])
{
    if (argc < 2) {
        printf("用法: %s <pub|sub>\n", argv[0]);
        printf("\n实验:\n");
        printf("  终端1: %s sub    ← 观察实例生命周期变化\n", argv[0]);
        printf("  终端2: %s pub    ← 发布3辆车, 演示 dispose/unregister\n", argv[0]);
        return 1;
    }

    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);

    if (strcmp(argv[1], "pub") == 0)
        run_publisher();
    else if (strcmp(argv[1], "sub") == 0)
        run_subscriber();
    else
        printf("未知参数: %s\n", argv[1]);

    return 0;
}
