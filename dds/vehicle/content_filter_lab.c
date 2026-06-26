/**
 * content_filter_lab.c — DDS Content Filter 实验
 *
 * DDS 支持在 Topic 上设置过滤函数, 不符合条件的样本在服务端直接丢弃,
 * 不会传输到订阅端, 节省带宽和 CPU。
 *
 * 用法:
 *   终端1: ./content_filter_lab sub           ← 不过滤, 收到全部
 *   终端2: ./content_filter_lab sub_filtered  ← speed>50 才收
 *   终端3: ./content_filter_lab pub           ← 发布 speed 0~100
 *
 * 工程意义:
 *   - 自动驾驶: 感知节点只订阅 <50m 内的障碍物
 *   - 机器人:   操作面板只订阅自己管控的机器人 ID
 *   - 车载:     导航只订阅同道路段的车辆
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <time.h>

#include "dds/dds.h"
#include "VehicleState.h"

#define DOMAIN_ID   0
#define TOPIC_NAME  "ContentFilterLab"

static volatile int running = 1;
static void sig_handler(int s) { (void)s; running = 0; }

/* ★ 过滤函数: 只允许 speed > 50 的数据通过 */
static bool speed_filter(const void* sample, void* arg)
{
    (void)arg;
    const vehicle_VehicleState* s = sample;
    return s->speed > 50.0;
}

/* ---- Publisher ---- */
static void run_publisher(void)
{
    dds_entity_t pp = dds_create_participant(DOMAIN_ID, NULL, NULL);
    dds_entity_t tp = dds_create_topic(pp, &vehicle_VehicleState_desc,
                                        TOPIC_NAME, NULL, NULL);

    dds_qos_t *q = dds_create_qos();
    dds_qset_reliability(q, DDS_RELIABILITY_RELIABLE, DDS_SECS(1));
    dds_qset_history(q, DDS_HISTORY_KEEP_LAST, 1);
    dds_entity_t wr = dds_create_writer(pp, tp, q, NULL);
    dds_delete_qos(q);

    printf("[PUB] 发布 speed=0~100 混合数据 (每 300ms)\n");
    printf("[PUB] ★ 有过滤器的订阅者只收到 speed>50 的数据\n\n");

    double speeds[] = {10, 25, 55, 80, 30, 60, 95, 15, 45, 70, 100, 5, 85, 40, 20};
    int total = sizeof(speeds) / sizeof(speeds[0]);

    for (int i = 0; i < total && running; i++) {
        vehicle_VehicleState s;
        memset(&s, 0, sizeof(s));
        snprintf(s.vehicle_id, 32, "car-%02d", i + 1);
        s.speed = speeds[i];
        s.battery_soc = 50.0 + (rand() % 50);
        s.status = s.speed > 50 ? vehicle_RUNNING : vehicle_STOPPED;

        dds_write(wr, &s);
        printf("[PUB] #%d speed=%.0f\n", i + 1, s.speed);
        usleep(300000);
    }

    dds_delete(wr); dds_delete(tp); dds_delete(pp);
}

/* ---- Subscriber (无过滤) ---- */
static void run_subscriber(bool filtered)
{
    dds_entity_t pp = dds_create_participant(DOMAIN_ID, NULL, NULL);
    dds_entity_t tp = dds_create_topic(pp, &vehicle_VehicleState_desc,
                                        TOPIC_NAME, NULL, NULL);

    /* ★ 在创建 Reader 之前设置 Content Filter */
    if (filtered) {
        dds_set_topic_filter_and_arg(tp, speed_filter, NULL);
    }

    dds_qos_t *q = dds_create_qos();
    dds_qset_reliability(q, DDS_RELIABILITY_RELIABLE, DDS_SECS(1));
    dds_qset_history(q, DDS_HISTORY_KEEP_LAST, 1);
    dds_entity_t rd = dds_create_reader(pp, tp, q, NULL);
    dds_delete_qos(q);

    const char* label = filtered ? "SUB_FILTERED (speed>50)" : "SUB_ALL";
    printf("[%s] 已启动 | Topic=%s\n", label, TOPIC_NAME);
    if (filtered)
        printf("[%s] ★ Content Filter 已设置 — 只接收 speed>50 的数据\n", label);
    else
        printf("[%s] 无过滤 — 接收全部数据\n", label);
    printf("[%s] 等待数据...\n\n", label);

    int count = 0, missed = 0;
    time_t start = time(NULL);

    while (running && time(NULL) - start < 10) {
        void* samples[4] = {0};
        dds_sample_info_t infos[4];
        memset(infos, 0, sizeof(infos));

        int n = dds_read(rd, samples, infos, 4, 4);
        for (int i = 0; i < n; i++) {
            if (!infos[i].valid_data) continue;
            vehicle_VehicleState* s = samples[i];
            count++;
            printf("[%s] #%d ← %s speed=%.0f\n", label, count, s->vehicle_id, s->speed);
        }
        if (n > 0) dds_return_loan(rd, samples, n);
        usleep(200000);
    }

    printf("\n[%s] 统计: 收到=%d 条\n", label, count);
    if (filtered) {
        printf("[%s] ★ 对比: pub 发了 15 条, 过滤后只收到速度>50的\n", label);
        printf("[%s] ★ 过滤丢弃的数据完全不占订阅端带宽\n", label);
    }

    dds_delete(rd); dds_delete(tp); dds_delete(pp);
}

int main(int argc, char* argv[])
{
    if (argc < 2) {
        printf("用法: %s <pub|sub|sub_filtered>\n", argv[0]);
        printf("\n实验:\n");
        printf("  终端1: %s sub              ← 收到全部 15 条\n", argv[0]);
        printf("  终端2: %s sub_filtered     ← 只收到 speed>50 的\n", argv[0]);
        printf("  终端3: %s pub              ← 发布混合数据\n", argv[0]);
        return 1;
    }

    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);

    if (strcmp(argv[1], "pub") == 0)
        run_publisher();
    else if (strcmp(argv[1], "sub") == 0)
        run_subscriber(false);
    else if (strcmp(argv[1], "sub_filtered") == 0)
        run_subscriber(true);
    else
        printf("未知参数: %s\n", argv[1]);

    return 0;
}
