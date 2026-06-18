/*
 * ethercat_slave.c — EtherCAT 虚拟从站
 *
 * 通过共享内存模拟一个 3 轴伺服从站 (Beckhoff EL6521 风格):
 *   - ESC 寄存器空间映射
 *   - SyncManager 2 (过程数据输出) 和 SM3 (过程数据输入)
 *   - FMMU 逻辑地址映射
 *   - DC 分布式时钟 (接收主站时间戳并计算漂移)
 *   - CoE 对象字典
 *
 * 与 ethercat_master 通过 /ethercat_esc_shm 共享内存通信。
 *
 * 用法: ./ethercat_slave
 */

#include "ethercat.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <time.h>
#include <math.h>
#include <errno.h>

#define SHM_NAME "/ethercat_esc_shm"

static volatile int g_running = 1;
static VirtualEsc *g_esc = NULL;

void sigint_handler(int sig) {
    (void)sig;
    g_running = 0;
}

// ==========================================================================
// CoE 对象字典 (精简版)
// ==========================================================================
typedef struct {
    ec_u16 index;
    ec_u8  sub_index;
    ec_u8  bit_length;
    ec_u32 value;
    const char *name;
} CoeObj;

static CoeObj g_coe_objects[] = {
    // 设备类型
    {0x1000, 0, 32, 0x00020192, "Device Type"},
    // 错误寄存器
    {0x1001, 0, 8,  0x00,       "Error Register"},
    // Sync Manager 通信类型
    {0x1C00, 0, 8,  4,          "Sync Manager Type count"},
    {0x1C00, 1, 8,  SM_TYPE_MAILBOX,  "SM0: Mailbox Out"},
    {0x1C00, 2, 8,  SM_TYPE_MAILBOX,  "SM1: Mailbox In"},
    {0x1C00, 3, 8,  SM_TYPE_OUTPUT,   "SM2: Processdata Out"},
    {0x1C00, 4, 8,  SM_TYPE_INPUT,    "SM3: Processdata In"},
    // RxPDO 分配 (SM2)
    {0x1C12, 0, 8,  1,          "RxPDO Assign count"},
    {0x1C12, 1, 16, 0x1600,     "RxPDO Assign[1]"},
    // TxPDO 分配 (SM3)
    {0x1C13, 0, 8,  1,          "TxPDO Assign count"},
    {0x1C13, 1, 16, 0x1A00,     "TxPDO Assign[1]"},
    // RxPDO 映射 (0x1600)
    {0x1600, 0, 8,  3,          "RxPDO Map count"},
    {0x1600, 1, 32, 0x607A0020, "RxPDO Map[1]: Target Pos Axis0"},
    {0x1600, 2, 32, 0x607A0120, "RxPDO Map[2]: Target Pos Axis1"},
    {0x1600, 3, 32, 0x607A0220, "RxPDO Map[3]: Target Pos Axis2"},
    // TxPDO 映射 (0x1A00)
    {0x1A00, 0, 8,  4,          "TxPDO Map count"},
    {0x1A00, 1, 32, 0x60640020, "TxPDO Map[1]: Actual Pos Axis0"},
    {0x1A00, 2, 32, 0x60640120, "TxPDO Map[2]: Actual Pos Axis1"},
    {0x1A00, 3, 32, 0x60640220, "TxPDO Map[3]: Actual Pos Axis2"},
    {0x1A00, 4, 16, 0x60410010, "TxPDO Map[4]: Statusword"},
    // DC 配置
    {0x1C32, 2, 32, 1000000,   "SM2 Cycle Time (1ms)"},
    {0x1C33, 2, 32, 1000000,   "SM3 Cycle Time (1ms)"},
};

static const int g_coe_obj_count = sizeof(g_coe_objects) / sizeof(g_coe_objects[0]);

// 查找 CoE 对象
static CoeObj* coe_find(ec_u16 index, ec_u8 sub_index) {
    for (int i = 0; i < g_coe_obj_count; i++) {
        if (g_coe_objects[i].index == index && g_coe_objects[i].sub_index == sub_index)
            return &g_coe_objects[i];
    }
    return NULL;
}

// ==========================================================================
// 虚拟 ESC 实现
// ==========================================================================
void virtual_esc_init(VirtualEsc *esc, int position) {
    memset(esc, 0, sizeof(*esc));
    esc->position = position;

    // ESC 类型信息
    esc->memory[ESC_REG_TYPE]       = 0x11;   // ET1200
    esc->memory[ESC_REG_REVISION]   = 0x01;
    esc->memory[ESC_REG_FMMU_COUNT] = 3;      // 3 个 FMMU
    esc->memory[ESC_REG_SM_COUNT]   = 4;      // 4 个 SyncManager
    esc->memory[ESC_REG_RAM_SIZE]   = 1;      // 1KB DPRAM
    esc->memory[ESC_REG_PORT_DESC]  = 0x07;   // 端口0/1: MII, 端口2/3: EBUS

    // DL 状态: LINK + LOOP_CLOSED
    *(ec_u16*)(esc->memory + ESC_REG_DL_STATUS) = 0x000D;

    // SM2 (过程数据输出, 主→从)
    esc->sm2_size = 12;  // 3 × I32
    *(ec_u16*)(esc->memory + ESC_REG_SM(2) + ESC_REG_SM_PHY_START) = 0x1000;
    *(ec_u16*)(esc->memory + ESC_REG_SM(2) + ESC_REG_SM_LENGTH)    = 512;
    esc->memory[ESC_REG_SM(2) + ESC_REG_SM_CTRL]  = (SM_TYPE_OUTPUT << 4) | SM_CTRL_ENABLE;

    // SM3 (过程数据输入, 从→主)
    esc->sm3_size = 14;  // 3 × I32 + U16
    *(ec_u16*)(esc->memory + ESC_REG_SM(3) + ESC_REG_SM_PHY_START) = 0x1200;
    *(ec_u16*)(esc->memory + ESC_REG_SM(3) + ESC_REG_SM_LENGTH)    = 512;
    esc->memory[ESC_REG_SM(3) + ESC_REG_SM_CTRL]  = (SM_TYPE_INPUT << 4) | SM_CTRL_ENABLE;

    // DC 初始化
    esc->dc_speed_cnt_per_sec = 1000000000ULL;  // 1GHz = 1ns 精度
    esc->memory[ESC_REG_DC_SPEED_CNT]     = (ec_u8)(esc->dc_speed_cnt_per_sec & 0xFF);
    esc->memory[ESC_REG_DC_SPEED_CNT + 1] = (ec_u8)((esc->dc_speed_cnt_per_sec >> 8) & 0xFF);
    esc->memory[ESC_REG_DC_SPEED_CNT + 2] = (ec_u8)((esc->dc_speed_cnt_per_sec >> 16) & 0xFF);
    esc->memory[ESC_REG_DC_SPEED_CNT + 3] = (ec_u8)((esc->dc_speed_cnt_per_sec >> 24) & 0xFF);

    // 初始状态: INIT
    esc->memory[ESC_REG_AL_STATUS] = EC_STATE_INIT;
    esc->state = EC_STATE_INIT;

    // 看门狗分频器
    esc->memory[ESC_REG_WDOG_DIV] = 0x09C4;  // 2500 → 100μs 基准
}

// AL 控制写入 → 状态机转换
static void esc_process_al_control(VirtualEsc *esc) {
    ec_u16 al_ctrl = *(ec_u16*)(esc->memory + ESC_REG_AL_CONTROL);
    EcState new_state = (EcState)(al_ctrl & EC_STATE_MASK);

    if (new_state != esc->state && new_state != EC_STATE_BOOT) {
        printf("[SLAVE#%d] State: %s → %s\n",
               esc->position,
               ec_state_str(esc->state),
               ec_state_str(new_state));

        esc->state = new_state;
        esc->memory[ESC_REG_AL_STATUS] = (ec_u8)(new_state & 0xFF);
        esc->memory[ESC_REG_AL_STATUS + 1] = 0x00;

        // AL 控制: 设置 ACK 位 + 状态 + 错误指示
        if (new_state == EC_STATE_INIT) {
            // INIT → 重置状态码
            *(ec_u16*)(esc->memory + ESC_REG_AL_STATUS_CODE) = 0x0000;
        }
    }

    // 复制请求状态到 AL 控制 (带 ACK)
    *(ec_u16*)(esc->memory + ESC_REG_AL_CONTROL) =
        (ec_u16)(esc->state | EC_AL_CTRL_ACK);
}

// 更新过程数据
static void esc_update_processdata(VirtualEsc *esc) {
    if (esc->state != EC_STATE_OP && esc->state != EC_STATE_SAFEOP)
        return;

    // 读取 SM2: 目标位置 (来自主站)
    memcpy(esc->axis_target_pos, esc->sm2_buffer, sizeof(esc->axis_target_pos));

    // 模拟伺服响应: 实际位置 = 目标位置 (带微小延迟和噪声)
    static ec_u64 step_count = 0;
    step_count++;

    for (int i = 0; i < 3; i++) {
        // 模拟简单的位置跟随
        esc->axis_actual_pos[i] = esc->axis_target_pos[i]
            + (ec_i32)(sin(step_count * 0.01 + i) * 50);
        esc->axis_actual_vel[i] = (ec_i32)(cos(step_count * 0.01 + i) * 200);
        esc->axis_actual_torque[i] = (ec_i32)(50 + 10 * sin(step_count * 0.02 + i));
    }

    // 写入 SM3: 实际位置 + 状态字
    memcpy(esc->sm3_buffer, esc->axis_actual_pos, sizeof(esc->axis_actual_pos));
    ec_u16 status = (esc->state == EC_STATE_OP) ? 0x0237 : 0x0211;
    memcpy(esc->sm3_buffer + 12, &status, sizeof(status));
}

void virtual_esc_update(VirtualEsc *esc) {
    // 更新时间戳
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    ec_u64 now = (ec_u64)ts.tv_sec * 1000000000ULL + (ec_u64)ts.tv_nsec;

    // 写入 DC 系统时间
    *(ec_u64*)(esc->memory + ESC_REG_DC_SYSTIME) = now;

    // 处理 AL 控制
    esc_process_al_control(esc);

    // 更新过程数据
    esc_update_processdata(esc);
}

// ==========================================================================
// 运行时循环
// ==========================================================================
int main(void) {
    signal(SIGINT, sigint_handler);
    printf("=== EtherCAT Virtual Slave #1 ===\n");
    printf("Type: Beckhoff EL6521 (3-axis servo simulator)\n\n");

    // ---- 1. 创建/打开共享内存 ----
    int shm_fd = shm_open(SHM_NAME, O_CREAT | O_RDWR, 0666);
    if (shm_fd < 0) {
        perror("shm_open");
        return 1;
    }

    if (ftruncate(shm_fd, sizeof(VirtualEsc)) < 0) {
        perror("ftruncate");
        close(shm_fd);
        shm_unlink(SHM_NAME);
        return 1;
    }

    g_esc = (VirtualEsc *)mmap(NULL, sizeof(VirtualEsc),
                                PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (g_esc == MAP_FAILED) {
        perror("mmap");
        close(shm_fd);
        shm_unlink(SHM_NAME);
        return 1;
    }

    // ---- 2. 初始化 ESC ----
    virtual_esc_init(g_esc, 1);
    printf("[SLAVE#1] ESC initialized\n");
    printf("[SLAVE#1] FMMU=%d, SM=%d, RAM=%dKB\n",
           g_esc->memory[ESC_REG_FMMU_COUNT],
           g_esc->memory[ESC_REG_SM_COUNT],
           g_esc->memory[ESC_REG_RAM_SIZE]);

    // 打印 CoE 对象字典
    printf("[SLAVE#1] CoE Objects:\n");
    for (int i = 0; i < g_coe_obj_count; i++) {
        CoeObj *o = &g_coe_objects[i];
        if (o->sub_index == 0)
            printf("          [%04X] %-30s = 0x%08X\n", o->index, o->name, o->value);
    }
    printf("[SLAVE#1] Waiting for master...\n\n");

    // ---- 3. 状态监控循环 ----
    EcState last_state = EC_STATE_INIT;
    ec_u64 t_last_print = 0;
    int loop_count = 0;

    while (g_running) {
        virtual_esc_update(g_esc);

        // 状态变化时打印
        if (g_esc->state != last_state) {
            last_state = g_esc->state;
        }

        // 每秒打印一次状态
        loop_count++;
        if (loop_count % 1000000 == 0) {
            struct timespec ts;
            clock_gettime(CLOCK_MONOTONIC, &ts);
            ec_u64 now = (ec_u64)ts.tv_sec * 1000000000ULL + (ec_u64)ts.tv_nsec;

            printf("[SLAVE#1] State=%s | WKC=0x%04X | Pos=[%d, %d, %d]\n",
                   ec_state_str(g_esc->state), g_esc->wkc,
                   g_esc->axis_actual_pos[0],
                   g_esc->axis_actual_pos[1],
                   g_esc->axis_actual_pos[2]);
        }

        usleep(1);  // ~1μs 间隔, 模拟飞读飞写
    }

    // ---- 4. 清理 ----
    printf("\n[SLAVE#1] Shutting down...\n");

    munmap(g_esc, sizeof(VirtualEsc));
    close(shm_fd);
    shm_unlink(SHM_NAME);

    printf("[SLAVE#1] Done.\n");
    return 0;
}
