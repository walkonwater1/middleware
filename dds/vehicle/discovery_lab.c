/**
 * discovery_lab.c — DDS 自动发现实验
 *
 * 启动 3 个进程 (pub1, pub2, sub), 不配 IP 不配端口,
 * 仅靠 Domain=0 + Topic="DiscoveryLab" 自动互相发现。
 *
 * 用法:
 *   ./discovery_lab pub1     # 终端1: 发布者1
 *   ./discovery_lab pub2     # 终端2: 发布者2
 *   ./discovery_lab sub      # 终端3: 订阅者 (自动收到两个 pub 的数据)
 *
 * 观察:
 *   - 三个进程启动顺序任意
 *   - 无需指定对方 IP/端口
 *   - sub 自动收到 pub1 和 pub2 的数据
 *   - 多播发现完成后自动切换到单播
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
#define TOPIC_NAME  "DiscoveryLab"

static volatile int running = 1;
static void sig_handler(int s) { (void)s; running = 0; }

static void run_publisher(const char *name)
{
    dds_entity_t pp = dds_create_participant(DOMAIN_ID, NULL, NULL);
    dds_entity_t tp = dds_create_topic(pp, &vehicle_VehicleState_desc,
                                        TOPIC_NAME, NULL, NULL);

    dds_qos_t *q = dds_create_qos();
    dds_qset_reliability(q, DDS_RELIABILITY_RELIABLE, DDS_SECS(1));
    dds_entity_t wr = dds_create_writer(pp, tp, q, NULL);
    dds_delete_qos(q);

    printf("[%s] 发布者已启动 | Domain=%d Topic=%s\n", name, DOMAIN_ID, TOPIC_NAME);
    printf("[%s] ★ 无需配置任何 IP/端口, 等待订阅者自动发现...\n\n", name);

    int seq = 0;
    while (running) {
        seq++;
        vehicle_VehicleState s;
        memset(&s, 0, sizeof(s));
        snprintf(s.vehicle_id, 32, "%s", name);
        s.speed = 30.0 + (rand() % 100) / 10.0;

        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        s.timestamp = (int64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;

        dds_write(wr, &s);
        printf("[%s] #%d speed=%.1f\n", name, seq, s.speed);
        sleep(2);
    }

    dds_delete(wr); dds_delete(tp); dds_delete(pp);
}

static void run_subscriber(void)
{
    dds_entity_t pp = dds_create_participant(DOMAIN_ID, NULL, NULL);
    dds_entity_t tp = dds_create_topic(pp, &vehicle_VehicleState_desc,
                                        TOPIC_NAME, NULL, NULL);

    dds_qos_t *q = dds_create_qos();
    dds_qset_reliability(q, DDS_RELIABILITY_RELIABLE, DDS_SECS(1));
    dds_entity_t rd = dds_create_reader(pp, tp, q, NULL);
    dds_delete_qos(q);

    printf("[sub] 订阅者已启动 | Domain=%d Topic=%s\n", DOMAIN_ID, TOPIC_NAME);
    printf("[sub] ★ 自动发现同 Domain 的所有发布者, 无需配置!\n\n");

    while (running) {
        void* samples[4] = {0};
        dds_sample_info_t infos[4];
        memset(infos, 0, sizeof(infos));
        int n = dds_read(rd, samples, infos, 4, 4);

        for (int i = 0; i < n; i++) {
            if (!infos[i].valid_data) continue;
            vehicle_VehicleState* s = samples[i];
            printf("[sub] ← 来自 %s | speed=%.1f  [publisher_id=%ld]\n",
                   s->vehicle_id, s->speed,
                   infos[i].publication_handle);
        }
        if (n > 0) dds_return_loan(rd, samples, n);
        usleep(500000);
    }

    dds_delete(rd); dds_delete(tp); dds_delete(pp);
}

int main(int argc, char *argv[])
{
    if (argc < 2) {
        printf("用法: %s <pub1|pub2|sub>\n", argv[0]);
        printf("  终端1: %s pub1\n", argv[0]);
        printf("  终端2: %s pub2\n", argv[0]);
        printf("  终端3: %s sub    <- 自动收到两个 pub 的数据\n", argv[0]);
        return 1;
    }

    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);

    if (strcmp(argv[1], "sub") == 0)
        run_subscriber();
    else
        run_publisher(argv[1]);

    return 0;
}
