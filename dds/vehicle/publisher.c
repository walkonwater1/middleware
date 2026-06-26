/**
 * DDS 发布者 Demo (C) — 车辆状态发布
 *
 * 依赖: Eclipse CycloneDDS C 库
 * 安装: sudo apt install cyclonedds-dev
 *
 * 工作流:
 *   1. 定义 IDL → idl/HelloWorld.idl
 *   2. idlc 生成 C 类型代码 → VehicleState.h / VehicleState.c
 *   3. 本文件使用生成的类型进行发布
 *
 * 发布主题: VehicleState (Domain 0)
 * QoS: Reliable + TransientLocal + KeepLast(10)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <time.h>

#include "dds/dds.h"
#include "VehicleState.h"  /* idlc 生成 */

/* ============================================================
 * 配置
 * ============================================================ */
#define DOMAIN_ID           0
#define TOPIC_NAME          "VehicleState"
#define PUBLISH_PERIOD_SEC  1

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
    dds_entity_t participant;
    dds_entity_t topic;
    dds_entity_t writer;
    dds_return_t rc;
    dds_qos_t   *qos;

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    /* 1. 创建 DomainParticipant */
    participant = dds_create_participant(DOMAIN_ID, NULL, NULL);
    if (participant < 0) {
        fprintf(stderr, "[ERR] 创建 Participant 失败, rc=%d\n", participant);
        return 1;
    }

    /* 2. 创建 Topic (使用 IDL 生成的描述符) */
    topic = dds_create_topic(participant, &vehicle_VehicleState_desc,
                             TOPIC_NAME, NULL, NULL);
    if (topic < 0) {
        fprintf(stderr, "[ERR] 创建 Topic 失败, rc=%d\n", topic);
        dds_delete(participant);
        return 1;
    }

    /* 3. 配置 QoS: Reliable + TransientLocal + KeepLast(10) */
    qos = dds_create_qos();
    dds_qset_reliability(qos, DDS_RELIABILITY_RELIABLE,
                         DDS_SECS(1));              /* max_blocking_time 1s */
    dds_qset_durability(qos, DDS_DURABILITY_TRANSIENT_LOCAL);
    dds_qset_history(qos, DDS_HISTORY_KEEP_LAST, 10);

    /* 4. 创建 DataWriter */
    writer = dds_create_writer(participant, topic, qos, NULL);
    if (writer < 0) {
        fprintf(stderr, "[ERR] 创建 Writer 失败, rc=%d\n", writer);
        dds_delete_qos(qos);
        dds_delete(participant);
        return 1;
    }
    dds_delete_qos(qos);

    printf("[PUB] DDS 车辆状态发布者已启动\n");
    printf("[PUB] Domain: %d, Topic: %s\n", DOMAIN_ID, TOPIC_NAME);
    printf("[PUB] QoS: Reliable + TransientLocal + KeepLast(10)\n\n");

    /* 5. 发布循环 */
    vehicle_VehicleState sample;
    memset(&sample, 0, sizeof(sample));
    strncpy(sample.vehicle_id, "dds-vehicle-001", sizeof(sample.vehicle_id) - 1);

    int seq = 0;
    const char *status_names[] = {"STOPPED", "RUNNING", "CHARGING", "ERROR"};

    while (running) {
        seq++;

        /* 模拟状态变化: 起步加速 → 巡航 → 故障停车 */
        if (seq <= 5) {
            sample.speed = seq * 10.0;
            sample.status = vehicle_RUNNING;
        } else if (seq <= 15) {
            sample.speed = 60.0 + ((rand() % 1000) - 500) / 100.0;
            sample.status = vehicle_RUNNING;
        } else {
            sample.speed = 0.0;
            sample.status = vehicle_ERROR;
        }

        /* 模拟位置 */
        sample.latitude   = 22.53 + seq * 0.001;
        sample.longitude  = 113.93 + seq * 0.001;

        /* 模拟电量 */
        sample.battery_soc = 85.0 - seq * 0.5 + (rand() % 40 - 20) / 100.0;
        if (sample.battery_soc < 10.0) sample.battery_soc = 10.0;

        /* 时间戳 (微秒) */
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        sample.timestamp = (int64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;

        /* 写入 */
        rc = dds_write(writer, &sample);
        if (rc != DDS_RETCODE_OK) {
            fprintf(stderr, "[ERR] 发布失败, rc=%d\n", rc);
        } else {
            printf("[PUB] #%03d | speed=%.1f km/h | pos=(%.4f,%.4f) | "
                   "battery=%.1f%% | status=%s\n",
                   seq, sample.speed, sample.latitude, sample.longitude,
                   sample.battery_soc, status_names[sample.status]);
        }

        sleep(PUBLISH_PERIOD_SEC);
    }

    printf("\n[PUB] 停止发布\n");

    /* 清理 */
    dds_delete(writer);
    dds_delete(topic);
    dds_delete(participant);
    return 0;
}
