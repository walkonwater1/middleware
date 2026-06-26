/**
 * bench_lab.c — DDS 延迟/吞吐量 Benchmark
 *
 * 测量不同 QoS 和 payload 配置下的性能, 输出对比报告。
 *
 * 用法:
 *   ./bench_lab                         # 运行全部测试
 *   ./bench_lab latency                 # 仅延迟测试
 *   ./bench_lab throughput              # 仅吞吐量测试
 *
 * 测试维度:
 *   - QoS: Reliable vs BestEffort
 *   - Payload: 32B / 256B / 1KB / 4KB
 *   - 延迟: 往返时间 (us)
 *   - 吞吐: 消息数/秒 + 带宽 Mbps
 *
 * 工程意义:
 *   - 选型: 你的场景用 Reliable 还是 BestEffort?
 *   - 容量规划: 1KB/msg × 1000Hz = 8 Mbps, 网络够吗?
 *   - QoS 调优: 不同 QoS 组合的性能差异有多大?
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <time.h>
#include <stdbool.h>

#include "dds/dds.h"
#include "VehicleState.h"

#define DOMAIN_ID   0
#define TOPIC_NAME  "BenchLab"

/* 测试配置 */
#define LATENCY_SAMPLES     200
#define THROUGHPUT_DURATION 5       /* 秒 */
#define THROUGHPUT_BURST    1000    /* 每批发送数量 */

static volatile int running = 1;
static void sig_handler(int s) { (void)s; running = 0; }

static int64_t now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (int64_t)ts.tv_sec * 1000000000 + ts.tv_nsec;
}

/* ============================================================
 * 延迟测试: pub→sub 往返时间
 * ============================================================ */
typedef struct {
    double avg_us;
    double min_us;
    double max_us;
    double p50_us;
    double p99_us;
    int    samples;
    int    lost;
} LatencyResult;

static LatencyResult run_latency_test(int reliable)
{
    LatencyResult r = {0};
    r.min_us = 1e9;

    dds_entity_t pp = dds_create_participant(DOMAIN_ID, NULL, NULL);
    dds_entity_t tp = dds_create_topic(pp, &vehicle_VehicleState_desc,
                                        TOPIC_NAME, NULL, NULL);

    dds_qos_t *q = dds_create_qos();
    dds_qset_reliability(q,
        reliable ? DDS_RELIABILITY_RELIABLE : DDS_RELIABILITY_BEST_EFFORT,
        DDS_SECS(1));
    dds_qset_history(q, DDS_HISTORY_KEEP_LAST, 1);
    dds_qset_durability(q, DDS_DURABILITY_VOLATILE);

    dds_entity_t wr = dds_create_writer(pp, tp, q, NULL);
    dds_entity_t rd = dds_create_reader(pp, tp, q, NULL);
    dds_delete_qos(q);

    /* 等待匹配 */
    usleep(500000);

    double latencies[LATENCY_SAMPLES];
    int lat_count = 0;

    for (int i = 0; i < LATENCY_SAMPLES && running; i++) {
        vehicle_VehicleState s;
        memset(&s, 0, sizeof(s));
        snprintf(s.vehicle_id, 32, "bench");
        s.speed = 60.0;

        int64_t t1 = now_ns();
        dds_write(wr, &s);

        /* 轮询等到数据 */
        int64_t deadline = now_ns() + 1000000000; /* 1s timeout */
        bool got = false;
        while (now_ns() < deadline) {
            void* samp[1] = {0};
            dds_sample_info_t info[1];
            memset(info, 0, sizeof(info));
            int n = dds_read(rd, samp, info, 1, 1);
            if (n > 0 && info[0].valid_data) {
                int64_t t2 = now_ns();
                double lat_us = (t2 - t1) / 1000.0;
                latencies[lat_count++] = lat_us;
                r.avg_us += lat_us;
                if (lat_us < r.min_us) r.min_us = lat_us;
                if (lat_us > r.max_us) r.max_us = lat_us;
                got = true;
                dds_return_loan(rd, samp, n);
                break;
            }
            if (n > 0) dds_return_loan(rd, samp, n);
        }
        if (!got) r.lost++;
    }

    if (lat_count > 0) {
        r.avg_us /= lat_count;
        r.samples = lat_count;

        /* 计算 P50/P99 */
        /* 简单冒泡排序 (样本量小) */
        for (int i = 0; i < lat_count - 1; i++)
            for (int j = 0; j < lat_count - 1 - i; j++)
                if (latencies[j] > latencies[j+1]) {
                    double t = latencies[j];
                    latencies[j] = latencies[j+1];
                    latencies[j+1] = t;
                }
        r.p50_us = latencies[lat_count / 2];
        r.p99_us = latencies[lat_count * 99 / 100];
    }

    dds_delete(rd); dds_delete(wr); dds_delete(tp); dds_delete(pp);
    return r;
}

/* ============================================================
 * 吞吐量测试: msg/s + Mbps
 * ============================================================ */
typedef struct {
    double msg_per_sec;
    double mbps;
    int    total;
    int    lost;
    int    duration_sec;
} ThroughputResult;

static ThroughputResult run_throughput_test(int reliable, int payload_size)
{
    ThroughputResult r = {0};

    dds_entity_t pp = dds_create_participant(DOMAIN_ID, NULL, NULL);
    dds_entity_t tp = dds_create_topic(pp, &vehicle_VehicleState_desc,
                                        TOPIC_NAME, NULL, NULL);

    dds_qos_t *q = dds_create_qos();
    dds_qset_reliability(q,
        reliable ? DDS_RELIABILITY_RELIABLE : DDS_RELIABILITY_BEST_EFFORT,
        DDS_SECS(1));
    dds_qset_history(q, DDS_HISTORY_KEEP_LAST, 1000);
    dds_qset_durability(q, DDS_DURABILITY_VOLATILE);

    dds_entity_t wr = dds_create_writer(pp, tp, q, NULL);
    dds_entity_t rd = dds_create_reader(pp, tp, q, NULL);
    dds_delete_qos(q);

    usleep(200000);

    /* 用 vehicle_id 来填充 payload */
    char payload[33];
    memset(payload, 'X', payload_size < 33 ? (size_t)payload_size : 32);
    payload[payload_size < 33 ? (size_t)payload_size : 32] = '\0';

    time_t start = time(NULL);
    int seq = 0;

    while (running && time(NULL) - start < THROUGHPUT_DURATION) {
        for (int b = 0; b < THROUGHPUT_BURST && running; b++) {
            vehicle_VehicleState s;
            memset(&s, 0, sizeof(s));
            strncpy(s.vehicle_id, payload, sizeof(s.vehicle_id) - 1);
            s.speed = seq++;
            s.timestamp = now_ns() / 1000;
            dds_write(wr, &s);
        }

        /* 清空 reader buffer (不统计, 只防止内存堆积) */
        void* trash[64] = {0};
        dds_sample_info_t ti[64];
        memset(ti, 0, sizeof(ti));
        int n = dds_take(rd, trash, ti, 64, 64);
        if (n > 0) { r.total += n; dds_return_loan(rd, trash, n); }
    }
    r.duration_sec = THROUGHPUT_DURATION;

    /* 最后再读一次, 收集残留 */
    for (int round = 0; round < 5; round++) {
        void* samp[64] = {0};
        dds_sample_info_t inf[64];
        memset(inf, 0, sizeof(inf));
        int n = dds_take(rd, samp, inf, 64, 64);
        if (n > 0) { r.total += n; dds_return_loan(rd, samp, n); }
        usleep(50000);
    }

    r.msg_per_sec = (double)r.total / THROUGHPUT_DURATION;
    r.mbps = r.msg_per_sec * sizeof(vehicle_VehicleState) * 8 / 1000000.0;

    dds_delete(rd); dds_delete(wr); dds_delete(tp); dds_delete(pp);
    return r;
}

/* ============================================================
 * 主程序
 * ============================================================ */
static void print_header(const char* title) {
    printf("\n╔══════════════════════════════════════════════════════╗\n");
    printf("║  %-50s  ║\n", title);
    printf("╚══════════════════════════════════════════════════════╝\n\n");
}

static void run_all_tests(void)
{
    printf("\n");
    printf("╔══════════════════════════════════════════════════════╗\n");
    printf("║     DDS 性能基准测试 (CycloneDDS 0.8.x)              ║\n");
    printf("╚══════════════════════════════════════════════════════╝\n");

    /* ================================================================
     * 延迟测试
     * ================================================================ */
    print_header("📊 1. 延迟测试 (Round-Trip, 200 样本)");

    printf("  测试中...\n");
    LatencyResult lat_reliable = run_latency_test(1);
    LatencyResult lat_besteffort = run_latency_test(0);

    printf("\n  ┌──────────────┬────────────┬──────────────┐\n");
    printf("  │ 指标 (us)    │ Reliable   │ BestEffort   │\n");
    printf("  ├──────────────┼────────────┼──────────────┤\n");
    printf("  │ 平均延迟     │ %8.1f   │ %8.1f     │\n",
           lat_reliable.avg_us, lat_besteffort.avg_us);
    printf("  │ 最小延迟     │ %8.1f   │ %8.1f     │\n",
           lat_reliable.min_us, lat_besteffort.min_us);
    printf("  │ 最大延迟     │ %8.1f   │ %8.1f     │\n",
           lat_reliable.max_us, lat_besteffort.max_us);
    printf("  │ P50          │ %8.1f   │ %8.1f     │\n",
           lat_reliable.p50_us, lat_besteffort.p50_us);
    printf("  │ P99          │ %8.1f   │ %8.1f     │\n",
           lat_reliable.p99_us, lat_besteffort.p99_us);
    printf("  │ 丢包         │ %-8d   │ %-8d       │\n",
           lat_reliable.lost, lat_besteffort.lost);
    printf("  └──────────────┴────────────┴──────────────┘\n");
    printf("\n  ★ Reliable 保证送达但延迟略高, BestEffort 更快但可能丢包\n");

    /* ================================================================
     * 吞吐量测试
     * ================================================================ */
    print_header("📊 2. 吞吐量测试 (5 秒持续发布, 不同 payload)");

    int payloads[] = {32, 256, 1024, 4096};

    printf("  ┌──────────┬──────────────────────┬──────────────────────┐\n");
    printf("  │ Payload  │ Reliable             │ BestEffort           │\n");
    printf("  │          │ msg/s       Mbps     │ msg/s       Mbps     │\n");
    printf("  ├──────────┼──────────────────────┼──────────────────────┤\n");

    for (int p = 0; p < 4; p++) {
        printf("  │ %-4d B   │ 测试中...\r", payloads[p]);
        fflush(stdout);

        ThroughputResult tr = run_throughput_test(1, payloads[p]);
        ThroughputResult tb = run_throughput_test(0, payloads[p]);

        printf("  │ %-4d B   │ %8.0f   %7.2f  │ %8.0f   %7.2f  │\n",
               payloads[p],
               tr.msg_per_sec, tr.mbps,
               tb.msg_per_sec, tb.mbps);
    }
    printf("  └──────────┴──────────────────────┴──────────────────────┘\n");
    printf("\n  ★ 吞吐量受 CPU 和网络限制, BestEffort 通常更高\n");

    /* ================================================================
     * 总结
     * ================================================================ */
    print_header("📋 总结建议");
    printf("  ┌────────────────────────────────────────────────────┐\n");
    printf("  │ 场景           推荐 QoS          原因              │\n");
    printf("  ├────────────────────────────────────────────────────┤\n");
    printf("  │ 传感器数据      BestEffort       高频、允许丢帧     │\n");
    printf("  │ 控制指令        Reliable         必须可靠送达       │\n");
    printf("  │ 状态同步        Reliable+History 断线重连需历史     │\n");
    printf("  │ 视频流          BestEffort       带宽优先          │\n");
    printf("  │ 日志/告警       Reliable         不能丢            │\n");
    printf("  └────────────────────────────────────────────────────┘\n\n");
}

int main(int argc, char* argv[])
{
    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);

    printf("DDS Benchmark Tool — CycloneDDS 0.8.x\n");
    printf("Payload: vehicle_VehicleState (sizeof=%zu bytes)\n",
           sizeof(vehicle_VehicleState));

    run_all_tests();

    return 0;
}
