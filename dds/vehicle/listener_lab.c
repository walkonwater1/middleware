/**
 * listener_lab.c — DDS Listener 模式 (回调 vs 轮询)
 *
 * 对比:
 *   subscriber.c  →  dds_read() 轮询  (Pull)
 *   本文件         →  dds_lset_data_available() 回调  (Push)
 *
 * ROS2 的 spin() 底层就是这种 Listener 模式,
 * 数据到达时立即触发回调, 无需轮询。
 *
 * 用法:
 *   终端1: ./publisher
 *   终端2: ./listener_lab
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>

#include "dds/dds.h"
#include "VehicleState.h"

#define DOMAIN_ID   0
#define TOPIC_NAME  "VehicleState"

static dds_entity_t participant = 0;
static dds_entity_t reader      = 0;
static volatile int running     = 1;
static int total_count          = 0;

/* ============================================================
 * 数据到达回调 (DDS Listener)
 *
 * 每当有新数据到达时 DDS 自动调用此函数,
 * 无需自己写轮询循环。 ← 这就是 ROS2 spin() 的本质
 * ============================================================ */
static void on_data_available(dds_entity_t rd, void *arg)
{
    (void)arg;

    void* samples[4] = {0};
    dds_sample_info_t infos[4];
    memset(infos, 0, sizeof(infos));

    int n = dds_read(rd, samples, infos, 4, 4);
    for (int i = 0; i < n; i++) {
        if (!infos[i].valid_data) continue;
        vehicle_VehicleState* s = samples[i];
        total_count++;

        const char *status_names[] = {"停止", "行驶", "充电", "异常"};
        printf("[Listener回调] #%d | %s speed=%.1f km/h SOC=%.1f%% status=%s\n",
               total_count,
               s->vehicle_id,
               s->speed,
               s->battery_soc,
               s->status >= 0 && s->status <= 3
                   ? status_names[s->status] : "?");
    }
    if (n > 0) dds_return_loan(rd, samples, n);
}

/* ============================================================
 * 订阅匹配回调
 *
 * 当有 Publisher 加入或离开时触发 ← 这就是自动发现的通知
 * ============================================================ */
static void on_subscription_matched(dds_entity_t rd, const dds_subscription_matched_status_t status, void *arg)
{
    (void)rd; (void)arg;
    printf("[Listener] ★ Publisher 匹配事件: 当前在线=%d, 变化=%d\n",
           status.current_count, status.current_count_change);
}

static void sig_handler(int s)
{
    (void)s;
    running = 0;
}

int main(void)
{
    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);

    /* 1. Participant */
    participant = dds_create_participant(DOMAIN_ID, NULL, NULL);
    if (participant < 0) {
        fprintf(stderr, "[ERR] Participant 创建失败\n");
        return 1;
    }

    /* 2. Topic + Reader */
    dds_entity_t topic = dds_create_topic(participant,
        &vehicle_VehicleState_desc, TOPIC_NAME, NULL, NULL);

    dds_qos_t *qos = dds_create_qos();
    dds_qset_reliability(qos, DDS_RELIABILITY_RELIABLE, DDS_SECS(1));
    dds_qset_durability(qos, DDS_DURABILITY_TRANSIENT_LOCAL);
    reader = dds_create_reader(participant, topic, qos, NULL);
    dds_delete_qos(qos);

    if (reader < 0) {
        fprintf(stderr, "[ERR] Reader 创建失败\n");
        dds_delete(participant);
        return 1;
    }

    /* 3. ★ 注册 Listener 回调 (替代 dds_read 轮询) */
    dds_listener_t *listener = dds_create_listener(NULL);
    dds_lset_data_available(listener, on_data_available);
    dds_lset_subscription_matched(listener, on_subscription_matched);
    dds_set_listener(reader, listener);

    printf("═══════════════════════════════════════\n");
    printf(" DDS Listener 模式订阅者\n");
    printf(" Topic: %s (Domain %d)\n", TOPIC_NAME, DOMAIN_ID);
    printf(" ★ 数据到达时自动回调, 无需轮询\n");
    printf(" ★ Publisher 加入/离开时也有回调通知\n");
    printf(" ★ 这就是 ROS2 spin() 的底层机制\n");
    printf("═══════════════════════════════════════\n\n");
    printf("等待 Publisher ... (请运行 ./publisher)\n\n");

    /* 4. ★ 进入休眠等待 (不轮询!) */
    while (running) {
        sleep(1);  // DDS 线程在后台处理, 数据到达时自动回调
    }

    printf("\n[EXIT] 共收到 %d 条消息\n", total_count);

    dds_delete_listener(listener);
    dds_delete(reader);
    dds_delete(topic);
    dds_delete(participant);
    return 0;
}
