/**
 * robot_dds_monitor.c — DDS 机器人远程监控终端 (Listener 回调模式)
 *
 * ★ 与旧版区别:
 *   - 3 个 Reader 全部使用 Listener 回调, 不再轮询
 *   - 数据到达立即显示 + 自动计算延迟
 *   - subscription_matched 自动检测服务端上线
 *   - 退出时打印性能报告
 *
 * 订阅 Topic (Listener 回调):
 *   - RobotStatus   → on_status()
 *   - TaskProgress  → on_progress()
 *   - RobotAlarm    → on_alarm()
 *
 * 发布 Topic:
 *   - TaskCommand (发送测试指令)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <time.h>

#include "dds/dds.h"
#include "robot.h"

#define DOMAIN_ID                  0
#define TOPIC_ROBOT_STATUS        "RobotStatus"
#define TOPIC_TASK_PROGRESS       "TaskProgress"
#define TOPIC_ROBOT_ALARM         "RobotAlarm"
#define TOPIC_TASK_COMMAND        "TaskCommand"
#define MONITOR_DURATION_SEC      25

/* ============================================================
 * 延迟统计
 * ============================================================ */
typedef struct {
    int    count;
    double total_latency_ms;
    double max_latency_ms;
} LatencyStats;

typedef struct {
    dds_entity_t  wr_command;
    LatencyStats  status_lat;
    LatencyStats  progress_lat;
    LatencyStats  alarm_lat;
    int           service_online;
} MonitorContext;

static volatile int running = 1;
static void signal_handler(int sig) { (void)sig; running = 0; }

static const char* state_name(int s) {
    switch (s) {
    case 0: return "Uninitialized"; case 1: return "Setup";
    case 2: return "SelfCheck";     case 3: return "Idle";
    case 4: return "Working";       case 5: return "LowPower";
    case 6: return "ManualControl"; case 7: return "Fault";
    case 8: return "EmergencyStop"; case 9: return "Terminated";
    default: return "Unknown";
    }
}

static const char* task_status_name(int s) {
    switch (s) {
    case 0: return "Starting"; case 1: return "Running";
    case 2: return "Completed"; case 3: return "Failed";
    default: return "Unknown";
    }
}

static int64_t now_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (int64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}

static void record_latency(LatencyStats* ls, double ms) {
    ls->count++;
    ls->total_latency_ms += ms;
    if (ms > ls->max_latency_ms) ls->max_latency_ms = ms;
}

/* ============================================================
 * ★ Listener 回调: RobotStatus
 * ============================================================ */
static void on_status(dds_entity_t rd, void* arg)
{
    MonitorContext* ctx = (MonitorContext*)arg;
    void* samples[4] = {0};
    dds_sample_info_t infos[4];
    memset(infos, 0, sizeof(infos));

    int n = dds_take(rd, samples, infos, 4, 4);
    for (int i = 0; i < n; i++) {
        if (!infos[i].valid_data) continue;
        robot_RobotStatus* s = samples[i];
        double lat = (now_us() - s->timestamp) / 1000.0;
        record_latency(&ctx->status_lat, lat);

        printf("[LISTENER] RobotStatus | state=%s batt=%.1f%% cpu=%.1fC "
               "emo=%s | lat=%.3fms\n",
               state_name(s->state), s->battery_level,
               s->cpu_temp, s->emotion, lat);
    }
    if (n > 0) dds_return_loan(rd, samples, n);
}

/* ★ Listener 回调: TaskProgress */
static void on_progress(dds_entity_t rd, void* arg)
{
    MonitorContext* ctx = (MonitorContext*)arg;
    void* samples[8] = {0};
    dds_sample_info_t infos[8];
    memset(infos, 0, sizeof(infos));

    int n = dds_take(rd, samples, infos, 8, 8);
    for (int i = 0; i < n; i++) {
        if (!infos[i].valid_data) continue;
        robot_TaskProgress* p = samples[i];
        double lat = (now_us() - p->timestamp) / 1000.0;
        record_latency(&ctx->progress_lat, lat);

        printf("[LISTENER] TaskProgress | %s %s/%s %.0f%% (%s) | lat=%.3fms\n",
               p->task_id, p->task_type, p->task_name,
               p->progress, task_status_name(p->status), lat);
    }
    if (n > 0) dds_return_loan(rd, samples, n);
}

/* ★ Listener 回调: RobotAlarm */
static void on_alarm(dds_entity_t rd, void* arg)
{
    MonitorContext* ctx = (MonitorContext*)arg;
    void* samples[8] = {0};
    dds_sample_info_t infos[8];
    memset(infos, 0, sizeof(infos));

    int n = dds_take(rd, samples, infos, 8, 8);
    for (int i = 0; i < n; i++) {
        if (!infos[i].valid_data) continue;
        robot_RobotAlarm* a = samples[i];
        double lat = (now_us() - a->timestamp) / 1000.0;
        record_latency(&ctx->alarm_lat, lat);

        printf("[LISTENER] ALARM [%s]: %s (value=%.1f) | lat=%.3fms\n",
               a->alarm_type, a->message, a->value, lat);
    }
    if (n > 0) dds_return_loan(rd, samples, n);
}

/* ★ Listener 回调: Service 上线/离线 */
static void on_service_matched(dds_entity_t rd,
                                const dds_subscription_matched_status_t st,
                                void* arg)
{
    (void)rd;
    MonitorContext* ctx = (MonitorContext*)arg;
    if (st.current_count_change > 0) {
        ctx->service_online = 1;
        printf("[LISTENER] *** Service 上线! (在线: %d) ***\n\n", st.current_count);
    } else {
        ctx->service_online = 0;
        printf("[LISTENER] *** Service 离线! ***\n");
    }
}

/* ============================================================
 * 发送指令
 * ============================================================ */
static void send_command(dds_entity_t wr, robot_CommandType type,
                         const char* task_type, const char* task_name,
                         const char* task_id) {
    robot_TaskCommand cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.command = type;
    if (task_type) strncpy(cmd.task_type, task_type, sizeof(cmd.task_type) - 1);
    if (task_name) strncpy(cmd.task_name, task_name, sizeof(cmd.task_name) - 1);
    if (task_id)   strncpy(cmd.task_id, task_id, sizeof(cmd.task_id) - 1);
    cmd.timestamp = now_us();
    dds_write(wr, &cmd);
}

/* ============================================================
 * 性能报告
 * ============================================================ */
static void print_perf(const char* name, LatencyStats* ls) {
    if (ls->count == 0) {
        printf("  %-15s: 无数据\n", name);
        return;
    }
    printf("  %-15s: %4d条 | avg=%.3fms | max=%.3fms\n",
           name, ls->count,
           ls->total_latency_ms / ls->count,
           ls->max_latency_ms);
}

/* ============================================================
 * 主函数
 * ============================================================ */
int main(void)
{
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    setbuf(stdout, NULL);

    printf("╔══════════════════════════════════════════╗\n");
    printf("║  DDS 机器人监控终端 (Listener 模式)      ║\n");
    printf("╚══════════════════════════════════════════╝\n\n");

    dds_entity_t pp = dds_create_participant(DOMAIN_ID, NULL, NULL);
    if (pp < 0) { fprintf(stderr, "[ERR] Participant failed\n"); return 1; }

    dds_entity_t tp_status   = dds_create_topic(pp, &robot_RobotStatus_desc,
                                                 TOPIC_ROBOT_STATUS, NULL, NULL);
    dds_entity_t tp_progress = dds_create_topic(pp, &robot_TaskProgress_desc,
                                                 TOPIC_TASK_PROGRESS, NULL, NULL);
    dds_entity_t tp_alarm    = dds_create_topic(pp, &robot_RobotAlarm_desc,
                                                 TOPIC_ROBOT_ALARM, NULL, NULL);
    dds_entity_t tp_command  = dds_create_topic(pp, &robot_TaskCommand_desc,
                                                 TOPIC_TASK_COMMAND, NULL, NULL);

    dds_qos_t* qos = dds_create_qos();
    dds_qset_reliability(qos, DDS_RELIABILITY_RELIABLE, DDS_SECS(1));
    dds_qset_durability(qos, DDS_DURABILITY_TRANSIENT_LOCAL);
    dds_qset_history(qos, DDS_HISTORY_KEEP_LAST, 10);

    dds_entity_t rd_status   = dds_create_reader(pp, tp_status, qos, NULL);
    dds_entity_t rd_progress = dds_create_reader(pp, tp_progress, qos, NULL);
    dds_entity_t rd_alarm    = dds_create_reader(pp, tp_alarm, qos, NULL);
    dds_entity_t wr_command  = dds_create_writer(pp, tp_command, qos, NULL);
    dds_delete_qos(qos);

    /* Context */
    MonitorContext ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.wr_command = wr_command;

    /* ★ 注册 3 个 Listener */
    dds_listener_t* l_status = dds_create_listener(&ctx);
    dds_lset_data_available(l_status, on_status);
    dds_lset_subscription_matched(l_status, on_service_matched);
    dds_set_listener(rd_status, l_status);

    dds_listener_t* l_progress = dds_create_listener(&ctx);
    dds_lset_data_available(l_progress, on_progress);
    dds_set_listener(rd_progress, l_progress);

    dds_listener_t* l_alarm = dds_create_listener(&ctx);
    dds_lset_data_available(l_alarm, on_alarm);
    dds_set_listener(rd_alarm, l_alarm);

    printf("[DDS] Domain: %d | QoS: Reliable+TransientLocal+KeepLast(10)\n", DOMAIN_ID);
    printf("[DDS] 订阅(Listener): RobotStatus, TaskProgress, RobotAlarm\n");
    printf("[DDS] 发布: TaskCommand\n");
    printf("[DDS] ★ 轮询已替换为 Listener 回调\n\n");

    /* 等待 Service 上线 */
    printf("[MONITOR] 等待 Service 上线...\n");
    int w = 0;
    while (running && !ctx.service_online && w < 50) {
        usleep(100000); w++;
    }
    if (!ctx.service_online) {
        printf("[MONITOR] 超时, 请先启动 robot_dds_service\n");
        goto cleanup;
    }
    sleep(1);

    /* 下发任务 */
    printf("[MONITOR] >>> 下发测试任务...\n");
    send_command(wr_command, robot_CMD_EXECUTE_TASK, "Navigation", "前往充电桩", "");
    usleep(300000);
    send_command(wr_command, robot_CMD_EXECUTE_TASK, "Motion", "挥手致意", "");
    usleep(300000);
    send_command(wr_command, robot_CMD_EXECUTE_TASK, "VoiceInteraction", "问答对话", "");
    printf("\n");

    /* 主循环: 只休眠, Listener 异步处理 */
    time_t start = time(NULL);
    int tick = 0;
    while (running) {
        sleep(1);
        tick++;
        if (tick % 5 == 0)
            printf("[MONITOR] [tick=%d] 监听中... (Listener 异步处理)\n", tick);
        if (time(NULL) - start >= MONITOR_DURATION_SEC) {
            printf("\n[MONITOR] 演示完成.\n");
            break;
        }
    }

    /* ★ 性能报告 */
    printf("\n╔══════════════════════════════════════════╗\n");
    printf("║  延迟性能报告 (Listener 模式)            ║\n");
    printf("╚══════════════════════════════════════════╝\n\n");
    print_perf("RobotStatus",   &ctx.status_lat);
    print_perf("TaskProgress",  &ctx.progress_lat);
    print_perf("RobotAlarm",    &ctx.alarm_lat);

    int total = ctx.status_lat.count + ctx.progress_lat.count
              + ctx.alarm_lat.count;
    double total_ms = ctx.status_lat.total_latency_ms
                    + ctx.progress_lat.total_latency_ms
                    + ctx.alarm_lat.total_latency_ms;
    if (total > 0)
        printf("  %-15s: %4d条 | avg=%.3fms\n", "总计", total, total_ms / total);

    printf("\n  ★ Listener 优势: 数据到达→us级回调 | CPU空闲休眠 | ROS2 spin()底层\n\n");

cleanup:
    dds_delete_listener(l_alarm);
    dds_delete_listener(l_progress);
    dds_delete_listener(l_status);
    dds_delete(wr_command);
    dds_delete(rd_alarm); dds_delete(rd_progress); dds_delete(rd_status);
    dds_delete(tp_command); dds_delete(tp_alarm); dds_delete(tp_progress); dds_delete(tp_status);
    dds_delete(pp);
    printf("[MONITOR] 已停止.\n");
    return 0;
}
