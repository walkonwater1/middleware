/**
 * robot_dds_service.c — DDS 机器人服务端
 *
 * 对标 EHR 架构:
 *   - robot_core     → 纯能力 (任务执行、状态通知)
 *   - robot_app      → 业务逻辑 (状态机、任务调度)
 *   - robot_dds      → DDS 通信 (发布/订阅)
 *
 * 发布 Topic:
 *   - RobotStatus   (每 1s):  状态、电量、CPU温度、表情
 *   - TaskProgress  (事件):    任务进度更新
 *   - RobotAlarm    (事件):    状态变更、低电量告警
 *
 * 订阅 Topic:
 *   - TaskCommand (事件):      接收任务指令、取消指令、急停
 *
 * 构建: 依赖 CycloneDDS + idlc 生成的 robot.h / robot.c
 * 运行: ./robot_dds_service
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <time.h>
#include <math.h>

#include "dds/dds.h"
#include "robot.h"  /* idlc 从 robot.idl 生成 */

/* ============================================================
 * 配置
 * ============================================================ */
#define DOMAIN_ID                  0

#define TOPIC_ROBOT_STATUS        "RobotStatus"
#define TOPIC_TASK_PROGRESS       "TaskProgress"
#define TOPIC_ROBOT_ALARM         "RobotAlarm"
#define TOPIC_TASK_COMMAND        "TaskCommand"

#define STATUS_PUBLISH_PERIOD_SEC 1
#define COMMAND_POLL_PERIOD_US    100000    /* 100ms */

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
 * 机器人状态枚举 (mirror IDL)
 * ============================================================ */
typedef enum {
    SIM_UNINITIALIZED = robot_STATE_UNINITIALIZED,
    SIM_SETUP         = robot_STATE_SETUP,
    SIM_SELF_CHECK    = robot_STATE_SELF_CHECK,
    SIM_IDLE          = robot_STATE_IDLE,
    SIM_WORKING       = robot_STATE_WORKING,
    SIM_LOW_POWER     = robot_STATE_LOW_POWER,
    SIM_MANUAL_CTRL   = robot_STATE_MANUAL_CONTROL,
    SIM_FAULT         = robot_STATE_FAULT,
    SIM_EMERGENCY     = robot_STATE_EMERGENCY_STOP,
    SIM_TERMINATED    = robot_STATE_TERMINATED
} SimRobotState;

const char* state_name(SimRobotState s) {
    switch (s) {
    case SIM_UNINITIALIZED: return "Uninitialized";
    case SIM_SETUP:         return "Setup";
    case SIM_SELF_CHECK:    return "SelfCheck";
    case SIM_IDLE:          return "Idle";
    case SIM_WORKING:       return "Working";
    case SIM_LOW_POWER:     return "LowPower";
    case SIM_MANUAL_CTRL:   return "ManualControl";
    case SIM_FAULT:         return "Fault";
    case SIM_EMERGENCY:     return "EmergencyStop";
    case SIM_TERMINATED:    return "Terminated";
    default:                 return "Unknown";
    }
}

/* ============================================================
 * 任务状态 (mirror IDL)
 * ============================================================ */
typedef enum {
    TK_STARTING  = robot_TASK_STARTING,
    TK_RUNNING   = robot_TASK_RUNNING,
    TK_COMPLETED = robot_TASK_COMPLETED,
    TK_FAILED    = robot_TASK_FAILED
} SimTaskStatus;

/* ============================================================
 * 模拟任务
 * ============================================================ */
#define MAX_TASKS 32

typedef struct {
    char     id[32];
    char     type[32];
    char     name[64];
    SimTaskStatus status;
    double   progress;      /* 0.0 - 100.0 */
} SimTask;

/* ============================================================
 * 机器人模拟器 (对应 RobotSimulator in gdbus demo)
 * ============================================================ */
typedef struct {
    /* 标识 */
    char        robot_id[32];

    /* 状态机 */
    SimRobotState state;
    SimRobotState last_emitted_state;  /* 用于检测状态变化 */

    /* 健康指标 */
    double      battery;
    double      cpu_temp;
    char        emotion[32];

    /* 任务调度 */
    SimTask     tasks[MAX_TASKS];
    int         task_count;
    int         task_counter;  /* 自增 ID */

    /* 统计 */
    int         tick;
} RobotSim;

static void robot_sim_init(RobotSim* r) {
    strcpy(r->robot_id, "ROBOT-DDS-001");
    r->state = SIM_UNINITIALIZED;
    r->last_emitted_state = SIM_UNINITIALIZED;
    r->battery = 87.0;
    r->cpu_temp = 42.0;
    strcpy(r->emotion, "Natural");
    r->task_count = 0;
    r->task_counter = 0;
    r->tick = 0;
}

/* 状态转移校验 (对应 kValidTransitions) */
static int is_valid_transition(SimRobotState from, SimRobotState to) {
    switch (from) {
    case SIM_UNINITIALIZED:
        return to == SIM_SETUP || to == SIM_FAULT || to == SIM_EMERGENCY || to == SIM_TERMINATED;
    case SIM_SETUP:
        return to == SIM_SELF_CHECK || to == SIM_FAULT || to == SIM_EMERGENCY || to == SIM_TERMINATED;
    case SIM_SELF_CHECK:
        return to == SIM_IDLE || to == SIM_FAULT || to == SIM_EMERGENCY || to == SIM_TERMINATED;
    case SIM_IDLE:
        return to == SIM_WORKING || to == SIM_LOW_POWER || to == SIM_MANUAL_CTRL
            || to == SIM_FAULT || to == SIM_EMERGENCY || to == SIM_TERMINATED;
    case SIM_WORKING:
        return to == SIM_IDLE || to == SIM_LOW_POWER || to == SIM_MANUAL_CTRL
            || to == SIM_FAULT || to == SIM_EMERGENCY || to == SIM_TERMINATED;
    case SIM_LOW_POWER:
        return to == SIM_IDLE || to == SIM_FAULT || to == SIM_EMERGENCY || to == SIM_TERMINATED;
    case SIM_MANUAL_CTRL:
        return to == SIM_IDLE || to == SIM_WORKING || to == SIM_FAULT || to == SIM_EMERGENCY || to == SIM_TERMINATED;
    case SIM_FAULT:
        return to == SIM_IDLE || to == SIM_EMERGENCY || to == SIM_TERMINATED;
    case SIM_EMERGENCY:
        return to == SIM_IDLE || to == SIM_TERMINATED;
    case SIM_TERMINATED:
        return 0;
    }
    return 0;
}

static void robot_sim_transition(RobotSim* r, SimRobotState to) {
    if (!is_valid_transition(r->state, to)) {
        printf("[SIM] ⚠ 非法转移: %s → %s (忽略)\n",
               state_name(r->state), state_name(to));
        return;
    }
    printf("[SIM] 状态变更: %s → %s\n", state_name(r->state), state_name(to));
    r->state = to;
}

/* 初始化流程: Uninitialized → Setup → SelfCheck → Idle */
static void robot_sim_initialize(RobotSim* r) {
    robot_sim_transition(r, SIM_SETUP);
    usleep(500000);   /* 模拟初始化耗时 */
    robot_sim_transition(r, SIM_SELF_CHECK);
    usleep(500000);
    robot_sim_transition(r, SIM_IDLE);
}

/* 添加任务 (Idle→Working, 对应 TaskOrchestrator::StartTask) */
static const char* robot_sim_add_task(RobotSim* r, const char* type, const char* name) {
    if (r->task_count >= MAX_TASKS) return NULL;

    SimTask* t = &r->tasks[r->task_count++];
    r->task_counter++;
    snprintf(t->id, sizeof(t->id), "TASK-%03d", r->task_counter);
    strncpy(t->type, type, sizeof(t->type) - 1);
    strncpy(t->name, name, sizeof(t->name) - 1);
    t->status = TK_STARTING;
    t->progress = 0.0;

    /* Idle → Working */
    if (r->state == SIM_IDLE) {
        robot_sim_transition(r, SIM_WORKING);
    }

    printf("[SIM] 下发任务: [%s] %s/%s\n", t->id, t->type, t->name);
    return t->id;
}

/* 取消任务 */
static int robot_sim_cancel_task(RobotSim* r, const char* task_id) {
    for (int i = 0; i < r->task_count; i++) {
        if (strcmp(r->tasks[i].id, task_id) == 0) {
            r->tasks[i].status = TK_FAILED;
            r->tasks[i].progress = 0.0;
            printf("[SIM] 取消任务: %s\n", task_id);
            return 1;
        }
    }
    return 0;
}

/* 获取活跃任务数 */
static int robot_sim_active_count(RobotSim* r) {
    int count = 0;
    for (int i = 0; i < r->task_count; i++) {
        if (r->tasks[i].status == TK_STARTING || r->tasks[i].status == TK_RUNNING)
            count++;
    }
    return count;
}

/* 更新任务进度 (每 1s 调用) */
static void robot_sim_update_tasks(RobotSim* r, double elapsed_sec) {
    for (int i = 0; i < r->task_count; i++) {
        SimTask* t = &r->tasks[i];
        if (t->status == TK_STARTING) {
            t->status = TK_RUNNING;
            t->progress = 5.0;
        } else if (t->status == TK_RUNNING) {
            t->progress += elapsed_sec * 20.0;  /* ~5s 完成 */
            if (t->progress >= 100.0) {
                t->progress = 100.0;
                t->status = TK_COMPLETED;
                printf("[TASK] %s: 完成\n", t->id);
            }
        }
    }

    /* 清理已完成/失败的任务 */
    int j = 0;
    for (int i = 0; i < r->task_count; i++) {
        if (r->tasks[i].status == TK_COMPLETED || r->tasks[i].status == TK_FAILED) {
            continue;  /* 丢弃 */
        }
        if (j != i) r->tasks[j] = r->tasks[i];
        j++;
    }
    r->task_count = j;

    /* Working → Idle */
    if (r->state == SIM_WORKING && robot_sim_active_count(r) == 0) {
        robot_sim_transition(r, SIM_IDLE);
    }
}

/* 更新健康指标 */
static void robot_sim_update_health(RobotSim* r, double elapsed_sec) {
    (void)elapsed_sec;

    /* CPU 温度波动 (Working 时更高) */
    double base = (r->state == SIM_WORKING) ? 58.0 : 42.0;
    double noise = ((rand() % 1000) - 500) / 1000.0;  /* -0.5 ~ +0.5 */
    r->cpu_temp = r->cpu_temp * 0.7 + (base + noise) * 0.3;

    /* 电量缓慢下降 */
    r->battery -= 0.1 + (rand() % 100) / 10000.0;  /* -0.1~-0.11 */
    if (r->battery < 0.0) r->battery = 100.0;  /* 模拟充电 */
    if (r->battery > 100.0) r->battery = 100.0;
}

/* 更新表情 */
static void robot_sim_update_emotion(RobotSim* r) {
    switch (r->state) {
    case SIM_IDLE:    strcpy(r->emotion, "Natural"); break;
    case SIM_WORKING: strcpy(r->emotion, "Thought"); break;
    case SIM_FAULT:   strcpy(r->emotion, "Anxiety"); break;
    case SIM_LOW_POWER: strcpy(r->emotion, "Grief"); break;
    default:          strcpy(r->emotion, "Natural"); break;
    }
}

/* ============================================================
 * 时间戳工具
 * ============================================================ */
static int64_t now_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (int64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}

/* ============================================================
 * DDS 发布: RobotStatus
 * ============================================================ */
static void publish_robot_status(dds_entity_t wr, RobotSim* r) {
    robot_RobotStatus sample;
    memset(&sample, 0, sizeof(sample));
    strncpy(sample.robot_id, r->robot_id, sizeof(sample.robot_id) - 1);
    sample.state = (robot_RobotState)r->state;
    sample.battery_level = r->battery;
    sample.cpu_temp = r->cpu_temp;
    strncpy(sample.emotion, r->emotion, sizeof(sample.emotion) - 1);
    sample.timestamp = now_us();

    dds_return_t rc = dds_write(wr, &sample);
    if (rc != DDS_RETCODE_OK) {
        fprintf(stderr, "[ERR] 发布 RobotStatus 失败, rc=%d\n", rc);
    }
}

/* ============================================================
 * DDS 发布: TaskProgress
 * ============================================================ */
static void publish_task_progress(dds_entity_t wr, const SimTask* t) {
    robot_TaskProgress sample;
    memset(&sample, 0, sizeof(sample));
    strncpy(sample.task_id, t->id, sizeof(sample.task_id) - 1);
    strncpy(sample.task_type, t->type, sizeof(sample.task_type) - 1);
    strncpy(sample.task_name, t->name, sizeof(sample.task_name) - 1);
    sample.status = (robot_TaskStatus)t->status;
    sample.progress = t->progress;
    sample.timestamp = now_us();

    dds_write(wr, &sample);
}

/* ============================================================
 * DDS 发布: RobotAlarm
 * ============================================================ */
static void publish_alarm(dds_entity_t wr, const char* type,
                          const char* msg, double value) {
    robot_RobotAlarm sample;
    memset(&sample, 0, sizeof(sample));
    strncpy(sample.alarm_type, type, sizeof(sample.alarm_type) - 1);
    strncpy(sample.message, msg, sizeof(sample.message) - 1);
    sample.value = value;
    sample.timestamp = now_us();

    dds_return_t rc = dds_write(wr, &sample);
    if (rc == DDS_RETCODE_OK) {
        printf("[ALARM] %s: %s (%.1f)\n", type, msg, value);
    }
}

/* ============================================================
 * DDS 订阅: TaskCommand (处理客户端指令)
 * ============================================================ */
static void process_commands(dds_entity_t rd, RobotSim* r,
                             dds_entity_t tp_wr, dds_entity_t alarm_wr) {
    void* samples[8] = {0};
    dds_sample_info_t infos[8];
    memset(infos, 0, sizeof(infos));

    dds_return_t rc = dds_take(rd, samples, infos, 8, 8);
    if (rc <= 0) return;

    for (int i = 0; i < rc; i++) {
        if (!infos[i].valid_data) continue;

        robot_TaskCommand* cmd = (robot_TaskCommand*)samples[i];
        switch (cmd->command) {
        case robot_CMD_EXECUTE_TASK: {
            /* 校验状态 (runability check — 对应 Bridge 层 IsRunnableState) */
            if (r->state == SIM_FAULT || r->state == SIM_EMERGENCY ||
                r->state == SIM_TERMINATED) {
                printf("[SERVICE] 拒绝任务: 机器人处于 %s 状态\n",
                       state_name(r->state));
                publish_alarm(alarm_wr, "TaskRejected",
                              "Robot in non-runnable state", (double)r->state);
                break;
            }
            const char* tid = robot_sim_add_task(r, cmd->task_type, cmd->task_name);
            if (tid) {
                /* 立即发布一次任务进度 (Starting) */
                for (int j = 0; j < r->task_count; j++) {
                    if (strcmp(r->tasks[j].id, tid) == 0) {
                        publish_task_progress(tp_wr, &r->tasks[j]);
                        break;
                    }
                }
            }
            break;
        }
        case robot_CMD_CANCEL_TASK: {
            if (robot_sim_cancel_task(r, cmd->task_id)) {
                /* 发布取消后的任务状态 */
                for (int j = 0; j < r->task_count; j++) {
                    if (strcmp(r->tasks[j].id, cmd->task_id) == 0) {
                        publish_task_progress(tp_wr, &r->tasks[j]);
                        break;
                    }
                }
            }
            break;
        }
        case robot_CMD_EMERGENCY_STOP: {
            printf("[SERVICE] ⚠ 收到紧急停止指令!\n");
            robot_sim_transition(r, SIM_EMERGENCY);
            /* 取消所有任务 */
            for (int j = 0; j < r->task_count; j++) {
                r->tasks[j].status = TK_FAILED;
                r->tasks[j].progress = 0.0;
            }
            r->task_count = 0;
            publish_alarm(alarm_wr, "EmergencyStop",
                          "Emergency stop triggered by remote", 0.0);
            break;
        }
        default:
            break;
        }
    }
    dds_return_loan(rd, samples, rc);
}

/* ============================================================
 * 主函数
 * ============================================================ */
int main(void)
{
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    printf("╔══════════════════════════════════════════╗\n");
    printf("║   机器人 DDS 服务端 (CycloneDDS C)      ║\n");
    printf("╚══════════════════════════════════════════╝\n\n");

    /* ---- Robot Simulator ---- */
    RobotSim robot;
    robot_sim_init(&robot);
    printf("[INFO] 机器人 ID: %s\n", robot.robot_id);
    printf("[INFO] 状态机:    Uninitialized → Setup → SelfCheck → Idle ↔ Working\n\n");

    printf("[INIT] 开始初始化...\n");
    robot_sim_initialize(&robot);
    printf("[INIT] 初始化完成, 当前状态: %s\n\n", state_name(robot.state));

    /* ---- DDS Participant ---- */
    dds_entity_t pp = dds_create_participant(DOMAIN_ID, NULL, NULL);
    if (pp < 0) {
        fprintf(stderr, "[ERR] 创建 Participant 失败, rc=%d\n", pp);
        return 1;
    }

    /* ---- DDS Topics ---- */
    dds_entity_t tp_status = dds_create_topic(pp, &robot_RobotStatus_desc,
                                              TOPIC_ROBOT_STATUS, NULL, NULL);
    dds_entity_t tp_progress = dds_create_topic(pp, &robot_TaskProgress_desc,
                                                TOPIC_TASK_PROGRESS, NULL, NULL);
    dds_entity_t tp_alarm = dds_create_topic(pp, &robot_RobotAlarm_desc,
                                             TOPIC_ROBOT_ALARM, NULL, NULL);
    dds_entity_t tp_command = dds_create_topic(pp, &robot_TaskCommand_desc,
                                               TOPIC_TASK_COMMAND, NULL, NULL);

    /* ---- DDS QoS: Reliable + TransientLocal + KeepLast(10) ---- */
    dds_qos_t* qos = dds_create_qos();
    dds_qset_reliability(qos, DDS_RELIABILITY_RELIABLE, DDS_SECS(1));
    dds_qset_durability(qos, DDS_DURABILITY_TRANSIENT_LOCAL);
    dds_qset_history(qos, DDS_HISTORY_KEEP_LAST, 10);

    /* ---- DDS Writers (发布端) ---- */
    dds_entity_t wr_status = dds_create_writer(pp, tp_status, qos, NULL);
    dds_entity_t wr_progress = dds_create_writer(pp, tp_progress, qos, NULL);
    dds_entity_t wr_alarm = dds_create_writer(pp, tp_alarm, qos, NULL);

    /* ---- DDS Reader (订阅端: TaskCommand) ---- */
    dds_entity_t rd_command = dds_create_reader(pp, tp_command, qos, NULL);

    dds_delete_qos(qos);

    printf("[DDS] Domain: %d\n", DOMAIN_ID);
    printf("[DDS] 发布 Topic: %s, %s, %s\n",
           TOPIC_ROBOT_STATUS, TOPIC_TASK_PROGRESS, TOPIC_ROBOT_ALARM);
    printf("[DDS] 订阅 Topic: %s\n", TOPIC_TASK_COMMAND);
    printf("[DDS] QoS: Reliable + TransientLocal + KeepLast(10)\n\n");

    /* ---- 发布初始状态 ---- */
    publish_robot_status(wr_status, &robot);
    publish_alarm(wr_alarm, "ServiceStarted",
                  "Robot DDS service online", robot.battery);
    printf("[SERVICE] 已启动, 按 Ctrl+C 退出\n\n");

    /* ---- 主循环 ---- */
    while (running) {
        /* 1. 处理收到的 TaskCommand (100ms 周期) */
        process_commands(rd_command, &robot, wr_progress, wr_alarm);

        /* 2. 每秒更新一次 */
        static time_t last_sec = 0;
        time_t now = time(NULL);
        if (now != last_sec) {
            last_sec = now;
            robot.tick++;

            /* 更新任务进度 */
            robot_sim_update_tasks(&robot, 1.0);

            /* 发布任务进度 (逐任务) */
            for (int i = 0; i < robot.task_count; i++) {
                const char* status_str = "Unknown";
                switch (robot.tasks[i].status) {
                case TK_STARTING:  status_str = "Starting"; break;
                case TK_RUNNING:   status_str = "Running"; break;
                case TK_COMPLETED: status_str = "Completed"; break;
                case TK_FAILED:    status_str = "Failed"; break;
                }
                printf("[TASK] %s: %.0f%% (%s)\n",
                       robot.tasks[i].id, robot.tasks[i].progress, status_str);
                publish_task_progress(wr_progress, &robot.tasks[i]);
            }

            /* 更新健康指标 + 表情 */
            robot_sim_update_emotion(&robot);
            robot_sim_update_health(&robot, 1.0);

            /* 发布 RobotStatus */
            publish_robot_status(wr_status, &robot);

            /* 状态变更告警 */
            if (robot.last_emitted_state != robot.state) {
                char msg[128];
                snprintf(msg, sizeof(msg), "State transition: %s → %s",
                         state_name(robot.last_emitted_state),
                         state_name(robot.state));
                publish_alarm(wr_alarm, "StateChanged", msg, (double)robot.state);
                robot.last_emitted_state = robot.state;
            }

            /* 低电量告警 */
            if (robot.battery < 15.0 && robot.state != SIM_LOW_POWER) {
                robot_sim_transition(&robot, SIM_LOW_POWER);
                publish_alarm(wr_alarm, "BatteryLow",
                              "Battery critically low!", robot.battery);
            }

            /* 定期打印状态 (每 5 tick) */
            if (robot.tick % 5 == 0) {
                printf("[STATUS] 状态=%s 电量=%.1f%% CPU=%.1f°C 表情=%s 活跃任务=%d\n",
                       state_name(robot.state), robot.battery,
                       robot.cpu_temp, robot.emotion,
                       robot_sim_active_count(&robot));
            }
        }

        usleep(COMMAND_POLL_PERIOD_US);
    }

    /* ---- 清理 ---- */
    printf("\n[SERVICE] 停止服务...\n");
    publish_alarm(wr_alarm, "ServiceStopped",
                  "Robot DDS service shutting down", 0.0);

    dds_delete(rd_command);
    dds_delete(wr_status);
    dds_delete(wr_progress);
    dds_delete(wr_alarm);
    dds_delete(tp_status);
    dds_delete(tp_progress);
    dds_delete(tp_alarm);
    dds_delete(tp_command);
    dds_delete(pp);

    printf("[SERVICE] 已停止.\n");
    return 0;
}
