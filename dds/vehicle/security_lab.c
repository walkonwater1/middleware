/**
 * security_lab.c — DDS Security 安全机制实验
 *
 * DDS Security 提供三层保护:
 *   1. Authentication   — 认证: 你是谁? (PKI 证书验证)
 *   2. Access Control   — 授权: 你能做什么? (读写权限控制)
 *   3. Cryptography     — 加密: 数据加密防窃听 (AES-GCM/SM4)
 *
 * CycloneDDS 通过 XML 配置文件加载 security plugins。
 * 配置文件指定 CA 证书、身份证书、governance 规则、permissions 规则。
 *
 * 用法:
 *   # 1. 生成安全配置 (只需一次)
 *   ./scripts/gen_security.sh
 *
 *   # 2. 启动安全 pub (Domain 0, 加密认证)
 *   CYCLONEDDS_URI=file://./security/config.xml ./security_lab pub
 *
 *   # 3. 启动安全 sub
 *   CYCLONEDDS_URI=file://./security/config.xml ./security_lab sub
 *
 *   # 4. 尝试无安全 pub ← 连不上! 被拒绝!
 *   ./security_lab pub_open
 *
 * 工程意义:
 *   - 军工: MILS 多级安全, 不同密级数据隔离
 *   - 自动驾驶: 防止恶意注入虚假障碍物数据
 *   - 医疗: HIPAA 患者数据加密传输
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
#define TOPIC_NAME  "SecurityLab"

static volatile int running = 1;
static void sig_handler(int s) { (void)s; running = 0; }

/* ---- Publisher (安全) ---- */
static void run_publisher(void)
{
    printf("╔══════════════════════════════════════════╗\n");
    printf("║  🔒 DDS Security — 安全发布者           ║\n");
    printf("╚══════════════════════════════════════════╝\n\n");

    /* Participant 创建时会根据 CYCLONEDDS_URI 加载 security plugins */
    dds_entity_t pp = dds_create_participant(DOMAIN_ID, NULL, NULL);
    if (pp < 0) {
        fprintf(stderr, "[ERR] 创建安全 Participant 失败! rc=%d\n", pp);
        fprintf(stderr, "[ERR] 请确认:\n");
        fprintf(stderr, "       1. 已运行 scripts/gen_security.sh 生成证书\n");
        fprintf(stderr, "       2. 已设置 CYCLONEDDS_URI 环境变量\n");
        return;
    }
    printf("[PUB] ✅ 安全 Participant 创建成功 (已认证)\n");

    dds_entity_t tp = dds_create_topic(pp, &vehicle_VehicleState_desc,
                                        TOPIC_NAME, NULL, NULL);
    dds_qos_t *q = dds_create_qos();
    dds_qset_reliability(q, DDS_RELIABILITY_RELIABLE, DDS_SECS(1));
    dds_entity_t wr = dds_create_writer(pp, tp, q, NULL);
    dds_delete_qos(q);

    printf("[PUB] ✅ 安全 Writer 创建成功\n");
    printf("[PUB] ★ 此通道数据经过加密, 无证书的节点无法窃听\n\n");

    for (int i = 0; i < 10 && running; i++) {
        vehicle_VehicleState s;
        memset(&s, 0, sizeof(s));
        snprintf(s.vehicle_id, 32, "secure-car");
        s.speed = 60.0 + i * 5;
        s.battery_soc = 85.0 - i;
        s.status = vehicle_RUNNING;

        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        s.timestamp = (int64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;

        int rc = dds_write(wr, &s);
        printf("[PUB] #%d speed=%.0f %s\n", i + 1, s.speed,
               rc == DDS_RETCODE_OK ? "✓" : "✗");
        sleep(1);
    }

    dds_delete(wr); dds_delete(tp); dds_delete(pp);
    printf("[PUB] 安全发布完成\n");
}

/* ---- Subscriber (安全) ---- */
static void run_subscriber(void)
{
    printf("╔══════════════════════════════════════════╗\n");
    printf("║  🔒 DDS Security — 安全订阅者           ║\n");
    printf("╚══════════════════════════════════════════╝\n\n");

    dds_entity_t pp = dds_create_participant(DOMAIN_ID, NULL, NULL);
    if (pp < 0) {
        fprintf(stderr, "[ERR] 创建安全 Participant 失败! rc=%d\n", pp);
        return;
    }
    printf("[SUB] ✅ 安全 Participant 创建成功\n");

    dds_entity_t tp = dds_create_topic(pp, &vehicle_VehicleState_desc,
                                        TOPIC_NAME, NULL, NULL);
    dds_qos_t *q = dds_create_qos();
    dds_qset_reliability(q, DDS_RELIABILITY_RELIABLE, DDS_SECS(1));
    dds_qset_history(q, DDS_HISTORY_KEEP_LAST, 10);
    dds_entity_t rd = dds_create_reader(pp, tp, q, NULL);
    dds_delete_qos(q);

    printf("[SUB] ✅ 安全 Reader 创建成功\n");
    printf("[SUB] ★ 等待安全发布者...\n\n");

    int count = 0;
    time_t start = time(NULL);

    while (running && time(NULL) - start < 15) {
        void* samples[4] = {0};
        dds_sample_info_t infos[4];
        memset(infos, 0, sizeof(infos));

        int n = dds_read(rd, samples, infos, 4, 4);
        for (int i = 0; i < n; i++) {
            if (!infos[i].valid_data) continue;
            vehicle_VehicleState* s = samples[i];
            count++;
            printf("[SUB] 🔒 #%d ← %s speed=%.0f SOC=%.1f%% | 解密成功 ✓\n",
                   count, s->vehicle_id, s->speed, s->battery_soc);
        }
        if (n > 0) dds_return_loan(rd, samples, n);
        usleep(200000);
    }

    printf("\n[SUB] 共收到 %d 条加密消息\n", count);
    dds_delete(rd); dds_delete(tp); dds_delete(pp);
}

/* ---- Publisher (无安全) — 尝试连接安全域, 预期失败 ---- */
static void run_open_publisher(void)
{
    printf("╔══════════════════════════════════════════╗\n");
    printf("║  🔓 无安全发布者 — 尝试入侵安全域        ║\n");
    printf("╚══════════════════════════════════════════╝\n\n");

    dds_entity_t pp = dds_create_participant(DOMAIN_ID, NULL, NULL);
    if (pp < 0) {
        printf("[OPEN] ❌ Participant 创建失败 (预期行为!)\n");
        printf("[OPEN] ★ 原因: 安全域要求认证, 无证书节点被拒绝\n");
        printf("[OPEN] ★ 这证明了 DDS Security 有效阻止了未授权接入\n");
        return;
    }

    /* 如果走到这里, 说明没有 security plugin 或配置不对 */
    printf("[OPEN] ⚠ Participant 创建成功 (安全未生效?)\n");
    printf("[OPEN] 提示: 系统可能未加载 security plugin\n");
    printf("[OPEN] 请确认 CYCLONEDDS_URI 指向安全配置文件\n");

    dds_delete(pp);
}

int main(int argc, char* argv[])
{
    if (argc < 2) {
        printf("用法: %s <pub|sub|pub_open>\n", argv[0]);
        printf("\nDDS Security 三层保护:\n");
        printf("  1. Authentication   — PKI 证书双向认证\n");
        printf("  2. Access Control   — 细粒度读写权限\n");
        printf("  3. Cryptography     — AES-GCM 数据加密\n");
        printf("\n运行步骤:\n");
        printf("  1. ./scripts/gen_security.sh        # 生成证书+配置\n");
        printf("  2. CYCLONEDDS_URI=file://./security/config.xml \\\n");
        printf("       %s pub &                      # 安全 pub\n", argv[0]);
        printf("  3. CYCLONEDDS_URI=file://./security/config.xml \\\n");
        printf("       %s sub                         # 安全 sub\n", argv[0]);
        printf("  4. %s pub_open   ← 尝试无安全接入, 应被拒绝!\n", argv[0]);
        return 1;
    }

    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);

    if (strcmp(argv[1], "pub") == 0)
        run_publisher();
    else if (strcmp(argv[1], "sub") == 0)
        run_subscriber();
    else if (strcmp(argv[1], "pub_open") == 0)
        run_open_publisher();
    else
        printf("未知参数: %s\n", argv[1]);

    return 0;
}
