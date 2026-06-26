/**
 * robot_dds_service.c — DDS 机器人服务端 (Listener 回调模式)
 *
 * 对标 EHR 架构:
 *   - robot_core     → 纯能力 (任务执行、状态通知)
 *   - robot_app      → 业务逻辑 (状态机、任务调度)
 *   - robot_dds      → DDS 通信 (Listener 回调, 非轮询)
 *
 * ★ 与旧版区别: TaskCommand 使用 Listener 回调接收,
 *   命令到达立即处理 (微秒级延迟), 不再 100ms 轮询。
 *
 * 发布 Topic:
 *   - RobotStatus   (每 1s 周期):  状态、电量、CPU温度、表情
 *   - TaskProgress  (事件, push):    任务进度更新
 *   - RobotAlarm    (事件, push):    状态变更、低电量告警
 *
 * 订阅 Topic:
 *   - TaskCommand (Listener 回调):  接收任务指令、取消、急停
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <time.h>
#include <math.h>

#include "dds/dds.h"
#include "robot.h"

/* ============================================================
 * 配置
 * ============================================================ */
#define DOMAIN_ID                  0

#define TOPIC_ROBOT_STATUS        "RobotStatus"
#define TOPIC_TASK_PROGRESS       "TaskProgress"
#define TOPIC_ROBOT_ALARM         "RobotAlarm"
#define TOPIC_TASK_COMMAND        "TaskCommand"

#define STATUS_PUBLISH_PERIOD_SEC 1

static volatile int running = 1;

/* ============================================================
 * 前向声明
 * ============================================================ */
typedef struct RobotSim RobotSim;
typedef struct ServiceContext ServiceContext;

/* ============================================================
 * 机器人状态枚举
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

typedef enum {
    TK_STARTING  = robot_TASK_STARTING,
    TK_RUNNING   = robot_TASK_RUNNING,
    TK_COMPLETED = robot_TASK_COMPLETED,
    TK_FAILED    = robot_TASK_FAILED
} SimTaskStatus;

#define MAX_TASKS 32

typedef struct {
    char          id[32];
    char          type[32];
    char          name[64];
    SimTaskStatus status;
    double        progress;
} SimTask;

struct RobotSim {
    char          robot_id[32];
    SimRobotState state;
    SimRobotState last_emitted_state;
    double        battery;
    double        cpu_temp;
    char          emotion[32];
    SimTask       tasks[MAX_TASKS];
    int           task_count;
    int           task_counter;
    int           tick;
};

/* ============================================================
 * Listener 回调上下文: 把 Writer 和 Simulator 传入回调
 * ============================================================ */
struct ServiceContext {
    RobotSim*   robot;
    dds_entity_t wr_progress;
    dds_entity_t wr_alarm;
    /* 延迟统计 */
    int         cmd_count;
    double      cmd_total_latency_us;
    double      cmd_max_latency_us;
};

/* ============================================================
 * 工具
 * ============================================================ */
static const char* state_name(SimRobotState s) {
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
    default:                return "Unknown";
    }
}

static int64_t now_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (int64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}

/* ============================================================
 * 状态机
 * ============================================================ */
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

static void robot_sim_initialize(RobotSim* r) {
    robot_sim_transition(r, SIM_SETUP);
    usleep(500000);
    robot_sim_transition(r, SIM_SELF_CHECK);
    usleep(500000);
    robot_sim_transition(r, SIM_IDLE);
}

static const char* robot_sim_add_task(RobotSim* r, const char* type, const char* name) {
    if (r->task_count >= MAX_TASKS) return NULL;
    SimTask* t = &r->tasks[r->task_count++];
    r->task_counter++;
    snprintf(t->id, sizeof(t->id), "TASK-%03d", r->task_counter);
    strncpy(t->type, type, sizeof(t->type) - 1);
    strncpy(t->name, name, sizeof(t->name) - 1);
    t->status = TK_STARTING;
    t->progress = 0.0;
    if (r->state == SIM_IDLE)
        robot_sim_transition(r, SIM_WORKING);
    printf("[SIM] 下发任务: [%s] %s/%s\n", t->id, t->type, t->name);
    return t->id;
}

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

static int robot_sim_active_count(RobotSim* r) {
    int c = 0;
    for (int i = 0; i < r->task_count; i++)
        if (r->tasks[i].status == TK_STARTING || r->tasks[i].status == TK_RUNNING)
            c++;
    return c;
}

static void robot_sim_update_tasks(RobotSim* r, double elapsed) {
    for (int i = 0; i < r->task_count; i++) {
        SimTask* t = &r->tasks[i];
        if (t->status == TK_STARTING) { t->status = TK_RUNNING; t->progress = 5.0; }
        else if (t->status == TK_RUNNING) {
            t->progress += elapsed * 20.0;
            if (t->progress >= 100.0) { t->progress = 100.0; t->status = TK_COMPLETED; }
        }
    }
    int j = 0;
    for (int i = 0; i < r->task_count; i++) {
        if (r->tasks[i].status == TK_COMPLETED || r->tasks[i].status == TK_FAILED) continue;
        if (j != i) r->tasks[j] = r->tasks[i];
        j++;
    }
    r->task_count = j;
    if (r->state == SIM_WORKING && robot_sim_active_count(r) == 0)
        robot_sim_transition(r, SIM_IDLE);
}

static void robot_sim_update_health(RobotSim* r, double elapsed) {
    (void)elapsed;
    double base = (r->state == SIM_WORKING) ? 58.0 : 42.0;
    double noise = ((rand() % 1000) - 500) / 1000.0;
    r->cpu_temp = r->cpu_temp * 0.7 + (base + noise) * 0.3;
    r->battery -= 0.1 + (rand() % 100) / 10000.0;
    if (r->battery < 0.0) r->battery = 100.0;
    if (r->battery > 100.0) r->battery = 100.0;
}

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
 * DDS 发布函数
 * ============================================================ */
static void publish_robot_status(dds_entity_t wr, RobotSim* r) {
    robot_RobotStatus s;
    memset(&s, 0, sizeof(s));
    strncpy(s.robot_id, r->robot_id, sizeof(s.robot_id) - 1);
    s.state = (robot_RobotState)r->state;
    s.battery_level = r->battery;
    s.cpu_temp = r->cpu_temp;
    strncpy(s.emotion, r->emotion, sizeof(s.emotion) - 1);
    s.timestamp = now_us();
    dds_write(wr, &s);
}

static void publish_task_progress(dds_entity_t wr, const SimTask* t) {
    robot_TaskProgress s;
    memset(&s, 0, sizeof(s));
    strncpy(s.task_id, t->id, sizeof(s.task_id) - 1);
    strncpy(s.task_type, t->type, sizeof(s.task_type) - 1);
    strncpy(s.task_name, t->name, sizeof(s.task_name) - 1);
    s.status = (robot_TaskStatus)t->status;
    s.progress = t->progress;
    s.timestamp = now_us();
    dds_write(wr, &s);
}

static void publish_alarm(dds_entity_t wr, const char* type,
                          const char* msg, double value) {
    robot_RobotAlarm s;
    memset(&s, 0, sizeof(s));
    strncpy(s.alarm_type, type, sizeof(s.alarm_type) - 1);
    strncpy(s.message, msg, sizeof(s.message) - 1);
    s.value = value;
    s.timestamp = now_us();
    int rc = dds_write(wr, &s);
    if (rc == DDS_RETCODE_OK)
        printf("[ALARM] %s: %s (%.1f)\n", type, msg, value);
}

/* ============================================================
 * ★ Listener 回调: TaskCommand 到达时立即触发
 *
 * 与旧版 process_commands() 的核心区别:
 *   - 旧: 主循环每 100ms 轮询 → 100ms 延迟
 *   - 新: DDS 内核数据到达立即回调 → 微秒级延迟
 *   - 这就是 ROS2 spin() 的底层机制
 * ============================================================ */
static void on_command_available(dds_entity_t rd, void* arg)
{
    ServiceContext* ctx = (ServiceContext*)arg;

    void* samples[8] = {0};
    dds_sample_info_t infos[8];
    memset(infos, 0, sizeof(infos));

    int n = dds_take(rd, samples, infos, 8, 8);
    for (int i = 0; i < n; i++) {
        if (!infos[i].valid_data) continue;

        robot_TaskCommand* cmd = (robot_TaskCommand*)samples[i];
        int64_t recv_time = now_us();
        double latency_us = (double)(recv_time - cmd->timestamp);

        /* 延迟统计 */
        ctx->cmd_count++;
        ctx->cmd_total_latency_us += latency_us;
        if (latency_us > ctx->cmd_max_latency_us)
            ctx->cmd_max_latency_us = latency_us;

        printf("[LISTENER] ⚡ 命令到达! 延迟=%.0fus type=%d\n",
               latency_us, cmd->command);

        switch (cmd->command) {
        case robot_CMD_EXECUTE_TASK: {
            if (ctx->robot->state == SIM_FAULT ||
                ctx->robot->state == SIM_EMERGENCY ||
                ctx->robot->state == SIM_TERMINATED) {
                printf("[SERVICE] 拒绝任务: %s 状态不可执行\n",
                       state_name(ctx->robot->state));
                publish_alarm(ctx->wr_alarm, "TaskRejected",
                              "Robot in non-runnable state",
                              (double)ctx->robot->state);
                break;
            }
            const char* tid = robot_sim_add_task(ctx->robot,
                                                  cmd->task_type, cmd->task_name);
            if (tid) {
                for (int j = 0; j < ctx->robot->task_count; j++) {
                    if (strcmp(ctx->robot->tasks[j].id, tid) == 0) {
                        publish_task_progress(ctx->wr_progress,
                                              &ctx->robot->tasks[j]);
                        break;
                    }
                }
            }
            break;
        }
        case robot_CMD_CANCEL_TASK: {
            if (robot_sim_cancel_task(ctx->robot, cmd->task_id)) {
                for (int j = 0; j < ctx->robot->task_count; j++) {
                    if (strcmp(ctx->robot->tasks[j].id, cmd->task_id) == 0) {
                        publish_task_progress(ctx->wr_progress,
                                              &ctx->robot->tasks[j]);
                        break;
                    }
                }
            }
            break;
        }
        case robot_CMD_EMERGENCY_STOP: {
            printf("[SERVICE] ⚠ 紧急停止!\n");
            robot_sim_transition(ctx->robot, SIM_EMERGENCY);
            for (int j = 0; j < ctx->robot->task_count; j++) {
                ctx->robot->tasks[j].status = TK_FAILED;
                ctx->robot->tasks[j].progress = 0.0;
            }
            ctx->robot->task_count = 0;
            publish_alarm(ctx->wr_alarm, "EmergencyStop",
                          "Emergency stop triggered by remote", 0.0);
            break;
        }
        default: break;
        }
    }
    if (n > 0) dds_return_loan(rd, samples, n);
}

/* ★ Listener 回调: Monitor 上线/离线通知 */
static void on_command_matched(dds_entity_t rd,
                                const dds_subscription_matched_status_t status,
                                void* arg)
{
    (void)rd; (void)arg;
    if (status.current_count_change > 0)
        printf("[LISTENER] ★ Monitor 上线! (当前在线: %d)\n",
               status.current_count);
    else
        printf("[LISTENER] ★ Monitor 离线! (当前在线: %d)\n",
               status.current_count);
}

/* ============================================================
 * 信号处理
 * ============================================================ */
static void signal_handler(int sig) { (void)sig; running = 0; }

/* ============================================================
 * 主函数
 * ============================================================ */
int main(void)
{
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    printf("╔══════════════════════════════════════════╗\n");
    printf("║   🤖 DDS 机器人服务端 (Listener 模式)   ║\n");
    printf("╚══════════════════════════════════════════╝\n\n");

    /* ---- 初始化机器人 ---- */
    RobotSim robot;
    robot_sim_init(&robot);
    printf("[INFO] 机器人 ID: %s\n", robot.robot_id);
    printf("[INFO] 状态机: Uninitialized→Setup→SelfCheck→Idle↔Working\n\n");

    printf("[INIT] 初始化...\n");
    robot_sim_initialize(&robot);
    printf("[INIT] 当前状态: %s\n\n", state_name(robot.state));

    /* ---- DDS ---- */
    dds_entity_t pp = dds_create_participant(DOMAIN_ID, NULL, NULL);
    if (pp < 0) { fprintf(stderr, "[ERR] Participant failed\n"); return 1; }

    dds_entity_t tp_status = dds_create_topic(pp, &robot_RobotStatus_desc,
                                               TOPIC_ROBOT_STATUS, NULL, NULL);
    dds_entity_t tp_progress = dds_create_topic(pp, &robot_TaskProgress_desc,
                                                 TOPIC_TASK_PROGRESS, NULL, NULL);
    dds_entity_t tp_alarm = dds_create_topic(pp, &robot_RobotAlarm_desc,
                                              TOPIC_ROBOT_ALARM, NULL, NULL);
    dds_entity_t tp_command = dds_create_topic(pp, &robot_TaskCommand_desc,
                                                TOPIC_TASK_COMMAND, NULL, NULL);

    dds_qos_t* qos = dds_create_qos();
    dds_qset_reliability(qos, DDS_RELIABILITY_RELIABLE, DDS_SECS(1));
    dds_qset_durability(qos, DDS_DURABILITY_TRANSIENT_LOCAL);
    dds_qset_history(qos, DDS_HISTORY_KEEP_LAST, 10);

    dds_entity_t wr_status   = dds_create_writer(pp, tp_status, qos, NULL);
    dds_entity_t wr_progress = dds_create_writer(pp, tp_progress, qos, NULL);
    dds_entity_t wr_alarm    = dds_create_writer(pp, tp_alarm, qos, NULL);
    dds_entity_t rd_command  = dds_create_reader(pp, tp_command, qos, NULL);
    dds_delete_qos(qos);

    /* ---- ★ 注册 Listener (替代轮询) ---- */
    ServiceContext ctx = {
        .robot = &robot,
        .wr_progress = wr_progress,
        .wr_alarm = wr_alarm,
        .cmd_count = 0,
        .cmd_total_latency_us = 0,
        .cmd_max_latency_us = 0,
    };

    dds_listener_t* listener = dds_create_listener(&ctx);
    dds_lset_data_available(listener, on_command_available);
    dds_lset_subscription_matched(listener, on_command_matched);
    dds_set_listener(rd_command, listener);

    printf("[DDS] Domain: %d\n", DOMAIN_ID);
    printf("[DDS] 发布: %s, %s, %s\n",
           TOPIC_ROBOT_STATUS, TOPIC_TASK_PROGRESS, TOPIC_ROBOT_ALARM);
    printf("[DDS] 订阅: %s (★ Listener 回调模式)\n", TOPIC_TASK_COMMAND);
    printf("[DDS] QoS: Reliable + TransientLocal + KeepLast(10)\n\n");

    /* 发布初始状态 */
    publish_robot_status(wr_status, &robot);
    publish_alarm(wr_alarm, "ServiceStarted", "Robot DDS service online (Listener mode)", robot.battery);
    printf("[SERVICE] 已启动, 等待指令... (Ctrl+C 退出)\n");
    printf("[SERVICE] ★ TaskCommand 使用 Listener 回调, 非轮询\n\n");

    /* ---- 主循环: 只做周期性更新 (1s), 命令由 Listener 异步处理 ---- */
    while (running) {
        static time_t last_sec = 0;
        time_t now = time(NULL);
        if (now != last_sec) {
            last_sec = now;
            robot.tick++;

            robot_sim_update_tasks(&robot, 1.0);
            robot_sim_update_emotion(&robot);
            robot_sim_update_health(&robot, 1.0);

            /* 发布任务进度 */
            for (int i = 0; i < robot.task_count; i++) {
                publish_task_progress(wr_progress, &robot.tasks[i]);
                if (robot.tasks[i].status == TK_COMPLETED)
                    printf("[TASK] %s: 完成\n", robot.tasks[i].id);
            }

            /* 发布 RobotStatus */
            publish_robot_status(wr_status, &robot);

            /* 状态变更告警 */
            if (robot.last_emitted_state != robot.state) {
                char msg[128];
                snprintf(msg, sizeof(msg), "State: %s→%s",
                         state_name(robot.last_emitted_state),
                         state_name(robot.state));
                publish_alarm(wr_alarm, "StateChanged", msg, (double)robot.state);
                robot.last_emitted_state = robot.state;
            }

            if (robot.battery < 15.0 && robot.state != SIM_LOW_POWER) {
                robot_sim_transition(&robot, SIM_LOW_POWER);
                publish_alarm(wr_alarm, "BatteryLow", "Battery critical!", robot.battery);
            }

            if (robot.tick % 5 == 0) {
                printf("[STATUS] state=%s battery=%.1f%% CPU=%.1f°C emo=%s tasks=%d\n",
                       state_name(robot.state), robot.battery,
                       robot.cpu_temp, robot.emotion,
                       robot_sim_active_count(&robot));
            }
        }
        sleep(1);  /* DDS 线程在后台处理 Listener 回调, 这里只休眠 */
    }

    /* ---- 清理 ---- */
    printf("\n[SERVICE] 停止...\n");
    if (ctx.cmd_count > 0) {
        printf("[PERF] 命令处理统计: 共%d条, 平均延迟=%.0fus, 最大延迟=%.0fus\n",
               ctx.cmd_count,
               ctx.cmd_total_latency_us / ctx.cmd_count,
               ctx.cmd_max_latency_us);
    }

    publish_alarm(wr_alarm, "ServiceStopped", "Robot DDS service shutting down", 0.0);
    dds_delete_listener(listener);
    dds_delete(rd_command);
    dds_delete(wr_status); dds_delete(wr_progress); dds_delete(wr_alarm);
    dds_delete(tp_status); dds_delete(tp_progress); dds_delete(tp_alarm); dds_delete(tp_command);
    dds_delete(pp);
    printf("[SERVICE] 已停止.\n");
    return 0;
}
