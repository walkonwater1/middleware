/**
 * robot_dds_monitor.c — DDS 机器人远程监控终端
 *
 * CycloneDDS 0.8.x loan API:
 *   void* samples[N] = {0};              // ★ 必须零初始化
 *   dds_sample_info_t infos[N] = {0};    // ★ 必须零初始化
 *   int n = dds_read(rd, samples, infos, N, N);
 *   robot_Xxx* data = samples[i];
 *   if (n > 0) dds_return_loan(rd, samples, n);
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
#define POLL_PERIOD_US            100000

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

static void send_command(dds_entity_t wr, robot_CommandType cmd_type,
                         const char* task_type, const char* task_name,
                         const char* task_id) {
    robot_TaskCommand cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.command = cmd_type;
    if (task_type) strncpy(cmd.task_type, task_type, sizeof(cmd.task_type) - 1);
    if (task_name) strncpy(cmd.task_name, task_name, sizeof(cmd.task_name) - 1);
    if (task_id)   strncpy(cmd.task_id, task_id, sizeof(cmd.task_id) - 1);
    cmd.timestamp = now_us();
    dds_write(wr, &cmd);
}

static void read_robot_status(dds_entity_t rd) {
    void* samples[4] = {0};
    dds_sample_info_t infos[4];
    memset(infos, 0, sizeof(infos));
    int n = dds_read(rd, samples, infos, 4, 4);
    for (int i = 0; i < n; i++) {
        if (!infos[i].valid_data) continue;
        robot_RobotStatus* s = samples[i];
        printf("[MONITOR] === 机器人状态 ===\n");
        printf("  Robot ID:    %s\n", s->robot_id);
        printf("  State:       %s (%d)\n", state_name(s->state), s->state);
        printf("  Battery:     %.1f%%\n", s->battery_level);
        printf("  CPU Temp:    %.1f°C\n", s->cpu_temp);
        printf("  Emotion:     %s\n", s->emotion);
        printf("========================\n");
    }
    if (n > 0) dds_return_loan(rd, samples, n);
}

static void read_task_progress(dds_entity_t rd) {
    void* samples[8] = {0};
    dds_sample_info_t infos[8];
    memset(infos, 0, sizeof(infos));
    int n = dds_read(rd, samples, infos, 8, 8);
    for (int i = 0; i < n; i++) {
        if (!infos[i].valid_data) continue;
        robot_TaskProgress* p = samples[i];
        printf("[MONITOR] <<< TaskProgress: %s | %s/%s | %.0f%% (%s)\n",
               p->task_id, p->task_type, p->task_name,
               p->progress, task_status_name(p->status));
    }
    if (n > 0) dds_return_loan(rd, samples, n);
}

static void read_robot_alarm(dds_entity_t rd) {
    void* samples[8] = {0};
    dds_sample_info_t infos[8];
    memset(infos, 0, sizeof(infos));
    int n = dds_read(rd, samples, infos, 8, 8);
    for (int i = 0; i < n; i++) {
        if (!infos[i].valid_data) continue;
        robot_RobotAlarm* a = samples[i];
        printf("[MONITOR] <<< ⚠ ALARM [%s]: %s (value=%.1f)\n",
               a->alarm_type, a->message, a->value);
    }
    if (n > 0) dds_return_loan(rd, samples, n);
}

int main(void)
{
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    setbuf(stdout, NULL);

    printf("╔══════════════════════════════════════════╗\n");
    printf("║   机器人远程监控终端 (CycloneDDS C)     ║\n");
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

    printf("[DDS] Domain: %d | QoS: Reliable+TransientLocal+KeepLast(10)\n", DOMAIN_ID);
    printf("[DDS] 订阅: RobotStatus, TaskProgress, RobotAlarm\n");
    printf("[DDS] 发布: TaskCommand\n\n");

    /* 等待服务端上线 */
    printf("[MONITOR] 等待服务端数据...\n");
    {
        int waited = 0;
        while (running && waited < 5000) {
            void* dummy[1] = {0};
            dds_sample_info_t info[1];
            memset(info, 0, sizeof(info));
            int n = dds_read(rd_status, dummy, info, 1, 1);
            if (n > 0) {
                int valid = info[0].valid_data;
                dds_return_loan(rd_status, dummy, n);
                if (valid) break;
            }
            usleep(200000);
            waited += 200;
        }
    }
    if (!running) goto cleanup;
    printf("[MONITOR] 已检测到服务端\n\n");
    sleep(1);

    /* 下发 3 个测试任务 */
    printf("[MONITOR] >>> 下发测试任务...\n");
    send_command(wr_command, robot_CMD_EXECUTE_TASK, "Navigation", "前往充电桩", "");
    usleep(300000);
    send_command(wr_command, robot_CMD_EXECUTE_TASK, "Motion", "挥手致意", "");
    usleep(300000);
    send_command(wr_command, robot_CMD_EXECUTE_TASK, "VoiceInteraction", "问答对话", "");
    printf("\n");

    /* 主监控循环 */
    time_t start_time = time(NULL);
    int tick = 0;

    while (running) {
        read_robot_status(rd_status);
        read_task_progress(rd_progress);
        read_robot_alarm(rd_alarm);

        tick++;
        if (tick % 10 == 0)
            printf("[MONITOR] [tick=%d] 监听中...\n", tick);

        if (time(NULL) - start_time >= MONITOR_DURATION_SEC) {
            printf("\n[MONITOR] 演示完成, 退出.\n");
            break;
        }
        usleep(POLL_PERIOD_US);
    }

cleanup:
    dds_delete(wr_command);
    dds_delete(rd_alarm); dds_delete(rd_progress); dds_delete(rd_status);
    dds_delete(tp_command); dds_delete(tp_alarm); dds_delete(tp_progress); dds_delete(tp_status);
    dds_delete(pp);
    printf("[MONITOR] 已停止.\n");
    return 0;
}
