/**
 * qos_lab.c — DDS QoS 实验
 *
 * 用法:
 *   ./qos_lab pub reliable  |  ./qos_lab sub reliable
 *   ./qos_lab pub besteffort|  ./qos_lab sub besteffort
 *   ./qos_lab pub transient |  ./qos_lab sub transient   (Late Joiner 测试)
 *
 * 实验清单:
 *   组合1: pub=reliable + sub=reliable   → 保证送达, 不丢数据
 *   组合2: pub=besteffort + sub=besteffort → 不保证送达, 可能丢数据
 *   组合3: pub=transient + sub=transient   → 先起 pub 再起 sub, sub 能拿到历史
 *   组合4: pub=volatile  + sub=volatile    → 先起 pub 再起 sub, sub 拿不到历史
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
#define TOPIC_NAME  "QoSLab"

static volatile int running = 1;
static void sig_handler(int s) { (void)s; running = 0; }

/* ---- Publisher ---- */
static void run_publisher(int reliable, int transient_local)
{
    dds_entity_t pp = dds_create_participant(DOMAIN_ID, NULL, NULL);
    dds_entity_t tp = dds_create_topic(pp, &vehicle_VehicleState_desc,
                                        TOPIC_NAME, NULL, NULL);

    dds_qos_t *q = dds_create_qos();
    dds_qset_reliability(q,
        reliable ? DDS_RELIABILITY_RELIABLE : DDS_RELIABILITY_BEST_EFFORT,
        DDS_SECS(1));
    dds_qset_durability(q,
        transient_local ? DDS_DURABILITY_TRANSIENT_LOCAL : DDS_DURABILITY_VOLATILE);
    dds_qset_history(q, DDS_HISTORY_KEEP_LAST, 10);

    dds_entity_t wr = dds_create_writer(pp, tp, q, NULL);
    dds_delete_qos(q);

    printf("[PUB] QoS 发布者已启动\n");
    printf("  Mode: %s + %s\n",
           reliable ? "RELIABLE" : "BEST_EFFORT",
           transient_local ? "TRANSIENT_LOCAL" : "VOLATILE");
    printf("  Domain=%d  Topic=%s\n", DOMAIN_ID, TOPIC_NAME);
    printf("  发布 10 条后退出...\n\n");

    for (int i = 0; i < 10 && running; i++) {
        vehicle_VehicleState s;
        memset(&s, 0, sizeof(s));
        snprintf(s.vehicle_id, 32, "qos-lab-001");
        s.speed        = (i + 1) * 10.0;
        s.battery_soc  = 80.0 - i * 2.0;
        s.status       = vehicle_RUNNING;

        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        s.timestamp = (int64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;

        int rc = dds_write(wr, &s);
        printf("[PUB] #%d speed=%.0f %s\n", i + 1, s.speed,
               rc == DDS_RETCODE_OK ? "✓" : "✗");
        sleep(1);
    }

    dds_delete(wr); dds_delete(tp); dds_delete(pp);
}

/* ---- Subscriber ---- */
static void run_subscriber(int reliable, int transient_local)
{
    dds_entity_t pp = dds_create_participant(DOMAIN_ID, NULL, NULL);
    dds_entity_t tp = dds_create_topic(pp, &vehicle_VehicleState_desc,
                                        TOPIC_NAME, NULL, NULL);

    dds_qos_t *q = dds_create_qos();
    dds_qset_reliability(q,
        reliable ? DDS_RELIABILITY_RELIABLE : DDS_RELIABILITY_BEST_EFFORT,
        DDS_SECS(1));
    dds_qset_durability(q,
        transient_local ? DDS_DURABILITY_TRANSIENT_LOCAL : DDS_DURABILITY_VOLATILE);
    dds_qset_history(q, DDS_HISTORY_KEEP_LAST, 10);

    dds_entity_t rd = dds_create_reader(pp, tp, q, NULL);
    dds_delete_qos(q);

    printf("[SUB] QoS 订阅者已启动\n");
    printf("  Mode: %s + %s\n",
           reliable ? "RELIABLE" : "BEST_EFFORT",
           transient_local ? "TRANSIENT_LOCAL" : "VOLATILE");
    printf("  Domain=%d  Topic=%s\n", DOMAIN_ID, TOPIC_NAME);
    printf("  ★ Late Joiner 测试: 如果 TRANSIENT_LOCAL, ");
    printf("现在加入也能收到之前发布的数据\n\n");

    int count = 0, missed = 0, last_seq = 0;
    time_t start = time(NULL);

    while (running && time(NULL) - start < 20) {
        void* samples[4] = {0};
        dds_sample_info_t infos[4];
        memset(infos, 0, sizeof(infos));

        int n = dds_read(rd, samples, infos, 4, 4);
        for (int i = 0; i < n; i++) {
            if (!infos[i].valid_data) continue;
            vehicle_VehicleState* s = samples[i];
            count++;
            int seq = (int)(s->speed / 10.0);
            if (last_seq > 0 && seq != last_seq + 1)
                missed += (seq - last_seq - 1);
            last_seq = seq;

            printf("[SUB] #%d speed=%.0f battery=%.1f%%  %s%s\n",
                   seq, s->speed, s->battery_soc,
                   infos[i].sample_state == DDS_ALIVE_INSTANCE_STATE ? "" : "(历史)",
                   missed > 0 ? "  ← 有丢帧!" : "");
        }
        if (n > 0) dds_return_loan(rd, samples, n);
        usleep(200000);
    }

    printf("\n[SUB] 统计: 收到=%d 丢失=%d\n", count, missed);
    dds_delete(rd); dds_delete(tp); dds_delete(pp);
}

/* ---- main ---- */
int main(int argc, char *argv[])
{
    if (argc < 3) {
        printf("用法: %s <pub|sub> <reliable|besteffort> [transient|volatile]\n",
               argv[0]);
        printf("实验:\n");
        printf("  ./qos_lab sub reliable\n");       // 先起
        printf("  ./qos_lab pub reliable\n");       // 再起 → 不丢帧
        printf("  ./qos_lab sub besteffort\n");
        printf("  ./qos_lab pub besteffort          → 可能丢帧\n");
        printf("  ./qos_lab pub transient  && sleep 5\n");
        printf("  ./qos_lab sub transient            → 拿到前5条历史\n");
        return 1;
    }

    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);

    int is_pub  = (strcmp(argv[1], "pub") == 0);
    int reliable = (strcmp(argv[2], "reliable") == 0);
    int transient = (argc > 3 && strcmp(argv[3], "transient") == 0);

    if (is_pub) run_publisher(reliable, transient);
    else        run_subscriber(reliable, transient);

    return 0;
}
