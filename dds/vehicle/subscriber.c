/**
 * DDS 订阅者 Demo (C) — 车辆状态订阅
 *
 * 依赖: Eclipse CycloneDDS C 库
 *
 * 订阅主题: VehicleState (Domain 0)
 * QoS: 与发布者匹配 (Reliable + TransientLocal + KeepLast(10))
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
#define DOMAIN_ID   0
#define TOPIC_NAME  "VehicleState"

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
    dds_entity_t reader;
    dds_return_t rc;
    dds_qos_t   *qos;

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    /* 1. DomainParticipant */
    participant = dds_create_participant(DOMAIN_ID, NULL, NULL);
    if (participant < 0) {
        fprintf(stderr, "[ERR] 创建 Participant 失败, rc=%d\n", participant);
        return 1;
    }

    /* 2. Topic */
    topic = dds_create_topic(participant, &vehicle_VehicleState_desc,
                             TOPIC_NAME, NULL, NULL);
    if (topic < 0) {
        fprintf(stderr, "[ERR] 创建 Topic 失败, rc=%d\n", topic);
        dds_delete(participant);
        return 1;
    }

    /* 3. QoS: 与发布者匹配 */
    qos = dds_create_qos();
    dds_qset_reliability(qos, DDS_RELIABILITY_RELIABLE, DDS_SECS(1));
    dds_qset_durability(qos, DDS_DURABILITY_TRANSIENT_LOCAL);
    dds_qset_history(qos, DDS_HISTORY_KEEP_LAST, 10);

    /* 4. DataReader */
    reader = dds_create_reader(participant, topic, qos, NULL);
    if (reader < 0) {
        fprintf(stderr, "[ERR] 创建 Reader 失败, rc=%d\n", reader);
        dds_delete_qos(qos);
        dds_delete(participant);
        return 1;
    }
    dds_delete_qos(qos);

    printf("[SUB] DDS 车辆状态订阅者已启动\n");
    printf("[SUB] Domain: %d, Topic: %s\n", DOMAIN_ID, TOPIC_NAME);
    printf("[SUB] 等待 VehicleState 数据...\n\n");

    /* 5. 接收循环 */
    const char *status_names[] = {"STOPPED", "RUNNING", "CHARGING", "ERROR"};
    int count = 0;

    while (running) {
        /* 批量读取 (CycloneDDS 0.8.x loan API) */
        void* samples[4] = {0};
        dds_sample_info_t infos[4];
        memset(infos, 0, sizeof(infos));
        rc = dds_read(reader, samples, infos, 4, 4);

        if (rc > 0) {
            for (int i = 0; i < rc; ++i) {
                if (!infos[i].valid_data) continue;

                count++;
                vehicle_VehicleState *msg = samples[i];

                /* 计算延迟 */
                struct timespec ts;
                clock_gettime(CLOCK_REALTIME, &ts);
                int64_t now_us = (int64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
                double latency_ms = (double)(now_us - msg->timestamp) / 1000.0;

                printf("[SUB] #%03d | id=%s | speed=%.1f km/h | "
                       "pos=(%.4f,%.4f) | battery=%.1f%% | "
                       "status=%s | 延迟=%.2f ms\n",
                       count,
                       msg->vehicle_id,
                       msg->speed,
                       msg->latitude, msg->longitude,
                       msg->battery_soc,
                       (msg->status >= 0 && msg->status <= 3)
                           ? status_names[msg->status] : "UNKNOWN",
                       latency_ms);

                /* 告警检测 */
                if (msg->status == vehicle_ERROR) {
                    printf("[SUB] ⚠ 告警: 车辆 %s 状态异常!\n",
                           msg->vehicle_id);
                }
                if (msg->battery_soc < 20.0) {
                    printf("[SUB] ⚠ 告警: 电量过低 (%.1f%%)!\n",
                           msg->battery_soc);
                }
            }
            dds_return_loan(reader, samples, rc);
        } else if (rc == DDS_RETCODE_TIMEOUT) {
            /* 超时, 无新数据，继续等待 */
        } else if (rc < 0) {
            fprintf(stderr, "[ERR] dds_read 失败, rc=%d\n", rc);
            break;
        }

        /* 短暂休眠, 避免忙等待 */
        usleep(100000);  /* 100ms */
    }

    printf("\n[SUB] 停止订阅\n");

    dds_delete(reader);
    dds_delete(topic);
    dds_delete(participant);
    return 0;
}
