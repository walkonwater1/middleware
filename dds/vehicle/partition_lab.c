/**
 * partition_lab.c — DDS Partition 隔离实验
 *
 * Partition 在同一个 Domain 内再做逻辑隔离:
 *   同一个 Partition 内的 Pub/Sub 互相可见
 *   不同 Partition 之间完全隔离
 *
 * 用法:
 *   终端1: ./partition_lab pub   A      # 只发到 Partition A
 *   终端2: ./partition_lab sub   A      # 订阅 Partition A → 收到
 *   终端3: ./partition_lab sub   B      # 订阅 Partition B → 收不到
 *
 * 用途:
 *   - 机器人: arm_partition / wheel_partition 互不干扰
 *   - 车载:   chassis / body / battery 各自隔离
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
#define TOPIC_NAME  "PartitionLab"

static volatile int running = 1;
static void sig_handler(int s) { (void)s; running = 0; }

static void run_publisher(const char *partition)
{
    dds_entity_t pp = dds_create_participant(DOMAIN_ID, NULL, NULL);

    // ★ 创建 Partitioned Topic
    dds_entity_t tp = dds_create_topic(pp, &vehicle_VehicleState_desc,
                                        TOPIC_NAME, NULL, NULL);

    dds_qos_t *q = dds_create_qos();
    dds_qset_reliability(q, DDS_RELIABILITY_RELIABLE, DDS_SECS(1));
    // ★ 设置 Partition
    dds_qset_partition(q, 1, &partition);

    dds_entity_t wr = dds_create_writer(pp, tp, q, NULL);
    dds_delete_qos(q);

    printf("[PUB] Partition=[%s] | Topic=%s\n", partition, TOPIC_NAME);
    printf("  ★ 只有订阅 [%s] 的节点能收到此数据\n\n", partition);

    int seq = 0;
    while (running) {
        seq++;
        vehicle_VehicleState s;
        memset(&s, 0, sizeof(s));
        snprintf(s.vehicle_id, 32, "part-%s", partition);
        s.speed = seq * 10.0;

        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        s.timestamp = (int64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;

        dds_write(wr, &s);
        printf("[PUB:%s] #%d speed=%.0f\n", partition, seq, s.speed);
        sleep(2);
    }

    dds_delete(wr); dds_delete(tp); dds_delete(pp);
}

static void run_subscriber(const char *partition)
{
    dds_entity_t pp = dds_create_participant(DOMAIN_ID, NULL, NULL);
    dds_entity_t tp = dds_create_topic(pp, &vehicle_VehicleState_desc,
                                        TOPIC_NAME, NULL, NULL);

    dds_qos_t *q = dds_create_qos();
    dds_qset_reliability(q, DDS_RELIABILITY_RELIABLE, DDS_SECS(1));
    dds_qset_partition(q, 1, &partition);  // ★ 只订阅此 Partition

    dds_entity_t rd = dds_create_reader(pp, tp, q, NULL);
    dds_delete_qos(q);

    printf("[SUB] Partition=[%s] | Topic=%s\n", partition, TOPIC_NAME);
    printf("  ★ 只接收 Partition [%s] 的数据\n", partition);
    printf("  ★ 其他 Partition 的数据不可见\n\n");

    int count = 0;
    time_t start = time(NULL);
    while (running && time(NULL) - start < 15) {
        void* samples[2] = {0};
        dds_sample_info_t infos[2];
        memset(infos, 0, sizeof(infos));
        int n = dds_read(rd, samples, infos, 2, 2);

        for (int i = 0; i < n; i++) {
            if (!infos[i].valid_data) continue;
            vehicle_VehicleState* s = samples[i];
            count++;
            printf("[SUB:%s] #%d <- %s speed=%.0f\n",
                   partition, count, s->vehicle_id, s->speed);
        }
        if (n > 0) dds_return_loan(rd, samples, n);
        usleep(500000);
    }

    printf("\n[SUB:%s] 共收到 %d 条 | ", partition, count);
    if (count == 0)
        printf("★ 同 Partition 内没有发布者, 完全隔离!\n");
    else
        printf("同 Partition 通信正常\n");

    dds_delete(rd); dds_delete(tp); dds_delete(pp);
}

int main(int argc, char *argv[])
{
    if (argc < 3) {
        printf("用法: %s <pub|sub> <Partition名>\n", argv[0]);
        printf("\n实验:\n");
        printf("  ./partition_lab pub A\n");
        printf("  ./partition_lab sub A   ← 收到\n");
        printf("  ./partition_lab sub B   ← 收不到! (隔离)\n");
        return 1;
    }

    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);

    if (strcmp(argv[1], "pub") == 0)
        run_publisher(argv[2]);
    else
        run_subscriber(argv[2]);

    return 0;
}
