/*
 * ethercat_master.c — EtherCAT 主站
 *
 * SOEM 风格 API 演示:
 *   1. ecx_init() — 初始化主站
 *   2. ecx_config_init() / ecx_config_map() — 配置从站 PDO 映射
 *   3. ecx_config_dc() — 分布式时钟配置
 *   4. ecx_statecheck() × 4 — 状态机推进 (INIT→PREOP→SAFEOP→OP)
 *   5. 主循环: ecx_send_processdata() / ecx_receive_processdata()
 *
 * 本 Demo 通过共享内存模拟 ESC, 无需真实硬件。
 *
 * 用法: ./ethercat_master
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

#define SHM_NAME "/ethercat_esc_shm"
#define CYCLE_US  1000        // 1kHz 控制周期
#define DC_CYCLE_NS 1000000   // 1ms
#define SIM_DURATION_S 15     // 运行 15 秒

static volatile int g_running = 1;

void sigint_handler(int sig) {
    (void)sig;
    g_running = 0;
}

// ----- 时间工具 -----
static ec_u64 get_time_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (ec_u64)ts.tv_sec * 1000000000ULL + (ec_u64)ts.tv_nsec;
}

// ==========================================================================
// 从站配置 (硬编码一个 EL6521 风格的 3轴伺服模块)
// ==========================================================================
static void setup_slave_pdo(EcSlave *slave) {
    // // RxPDO (主站→从站): 3 轴目标位置
    // 0x1600: RxPDO Mapping
    slave->rx_pdo[0].sm = 2;
    slave->rx_pdo[0].num_entries = 3;
    slave->rx_pdo[0].entries[0] = (PdoEntry){0x607A, 0x00, 32};  // axis0 target
    slave->rx_pdo[0].entries[1] = (PdoEntry){0x607A, 0x01, 32};  // axis1 target
    slave->rx_pdo[0].entries[2] = (PdoEntry){0x607A, 0x02, 32};  // axis2 target
    slave->rx_pdo[0].total_bit_length = 96;

    // // TxPDO (从站→主站): 3 轴实际位置 + 状态
    // 0x1A00: TxPDO Mapping
    slave->tx_pdo[0].sm = 3;
    slave->tx_pdo[0].num_entries = 4;
    slave->tx_pdo[0].entries[0] = (PdoEntry){0x6064, 0x00, 32};  // axis0 actual
    slave->tx_pdo[0].entries[1] = (PdoEntry){0x6064, 0x01, 32};  // axis1 actual
    slave->tx_pdo[0].entries[2] = (PdoEntry){0x6064, 0x02, 32};  // axis2 actual
    slave->tx_pdo[0].entries[3] = (PdoEntry){0x6041, 0x00, 16};  // statusword
    slave->tx_pdo[0].total_bit_length = 112;

    // 过程数据大小
    slave->input_size  = 14;  // 3×4(位置) + 2(状态)
    slave->output_size = 12;  // 3×4(目标位置)
}

// ==========================================================================
// 模拟从站 — 通过共享内存通信
// ==========================================================================
typedef struct {
    VirtualEsc esc;
    ec_u8 process_data_output[512];
    ec_u8 process_data_input[512];
} SlaveShm;

// ==========================================================================
// 主站实现
// ==========================================================================
static EcxContext g_ctx;
static SlaveShm *g_shm = NULL;
static ec_u16 g_working_counter = 0;
static ec_u64 g_last_send_ns = 0;

int ecx_init(EcxContext *ctx) {
    printf("[MASTER] Initializing EtherCAT master...\n");

    memset(ctx, 0, sizeof(*ctx));
    ctx->dc_config.sync0_cycle_ns = DC_CYCLE_NS;
    ctx->dc_config.sync0_shift_ns = 1000000; // 1ms
    ctx->dc_config.activate = 0x01;           // 仅 Sync0

    return 1;
}

int ecx_config_init(EcxContext *ctx, EcSlave *slave, int slave_count) {
    printf("[MASTER] Configuring %d slave(s)...\n", slave_count);

    ctx->slaves = slave;
    ctx->slave_count = slave_count;

    for (int i = 0; i < slave_count; i++) {
        slave[i].state = EC_STATE_INIT;
        slave[i].position = i + 1;
        slave[i].vendor_id = 0x00000002;     // Beckhoff
        slave[i].product_code = 0x06520000;  // EL6521

        // 配置 SM
        slave[i].sm_count = 4;
        slave[i].sm_type[0] = SM_TYPE_MAILBOX;    // SM0: 邮箱输出
        slave[i].sm_type[1] = SM_TYPE_MAILBOX;    // SM1: 邮箱输入
        slave[i].sm_type[2] = SM_TYPE_OUTPUT;     // SM2: 过程数据输出
        slave[i].sm_type[3] = SM_TYPE_INPUT;      // SM3: 过程数据输入

        printf("[MASTER]   Slave #%d: 0x%08X/0x%08X\n",
               i + 1, slave[i].vendor_id, slave[i].product_code);
    }

    return 1;
}

int ecx_config_map(EcxContext *ctx, EcSlave *slave) {
    printf("[MASTER] Mapping PDO for slave #%d...\n", slave->position);

    setup_slave_pdo(slave);

    printf("[MASTER]   RxPDO: %u bytes (SM2)\n", slave->output_size);
    printf("[MASTER]   TxPDO: %u bytes (SM3)\n", slave->input_size);

    ctx->processdata_size = slave->input_size + slave->output_size;

    return 1;
}

int ecx_config_dc(EcxContext *ctx, EcSlave *slave) {
    printf("[MASTER] Configuring Distributed Clock...\n");
    printf("[MASTER]   Sync0 cycle: %u ns (%.2f kHz)\n",
           ctx->dc_config.sync0_cycle_ns,
           1e9 / ctx->dc_config.sync0_cycle_ns / 1000.0);
    printf("[MASTER]   Sync0 shift: %u ns\n", ctx->dc_config.sync0_shift_ns);

    slave->dc.dc_supported = 1;
    ctx->dc_active = 1;

    return 1;
}

int ecx_statecheck(EcxContext *ctx, EcSlave *slave,
                    EcState req_state, int timeout_ms) {
    (void)ctx;
    (void)timeout_ms;

    // 在虚拟总线上: AL 控制写 + AL 状态回读
    slave->requested_state = req_state;
    slave->state = req_state;

    printf("[MASTER] Slave #%d → %s\n",
           slave->position, ec_state_str(req_state));

    return 1;
}

/// 在帧末尾追加一个数据报
static ec_u8* append_datagram(ec_u8 *frame, ec_u8 cmd, ec_u32 address,
                              ec_u16 data_len, const ec_u8 *data, int more)
{
    frame[0]  = cmd;
    frame[1]  = 0;
    frame[2]  = (ec_u8)(address & 0xFF);
    frame[3]  = (ec_u8)((address >> 8) & 0xFF);
    frame[4]  = (ec_u8)((address >> 16) & 0xFF);
    frame[5]  = (ec_u8)((address >> 24) & 0xFF);
    frame[6]  = (ec_u8)(data_len & 0xFF);
    frame[7]  = (ec_u8)((data_len >> 8) & 0xFF);
    frame[8]  = 0;
    frame[9]  = (ec_u8)more;
    if (data && data_len > 0)
        memcpy(frame + 10, data, data_len);
    return frame + 10 + data_len;
}

int ecx_send_processdata(EcxContext *ctx) {
    if (!g_shm) return 0;

    ctx->cycle_count++;

    ec_i32 targets[3] = {
        1024 + 500 * (ec_i32)(ctx->cycle_count % 200),
        2048 - 200 * (ec_i32)(ctx->cycle_count % 150),
        -512 + 300 * (ec_i32)(ctx->cycle_count % 100),
    };

    memcpy(ctx->processdata_output, targets, sizeof(targets));

    /* 构造 EtherCAT 数据报帧 */
    ec_u8 frame[256];
    ec_u8 *p = frame;

    /* 数据报1: LWR → SM2 (写入目标位置) */
    ec_u32 sm2_addr = 0x10000002;
    p = append_datagram(p, EC_CMD_LWR, sm2_addr, sizeof(targets),
                         (ec_u8*)targets, 1);

    /* 数据报2: LRD → SM3 (读取实际位置+状态) */
    ec_u32 sm3_addr = 0x10000003;
    ec_u8 dummy[14] = {0};
    p = append_datagram(p, EC_CMD_LRD, sm3_addr, 14, dummy, 0);

    ec_u16 frame_len = (ec_u16)(p - frame);
    memcpy(g_shm->esc.frame_in, frame, frame_len);
    g_shm->esc.frame_in_len = frame_len;
    g_shm->esc.frame_ready = 1;

    g_shm->esc.dc_time = get_time_ns();
    g_last_send_ns = g_shm->esc.dc_time;

    return 1;
}

int ecx_receive_processdata(EcxContext *ctx, int timeout_ms) {
    (void)timeout_ms;
    if (!g_shm) return 0;

    /* 等待从站处理完成 */
    int spin = 0;
    while (!g_shm->esc.response_ready && spin < 100000) spin++;

    if (!g_shm->esc.response_ready) return 0;

    /* 从 SM3 缓冲区读取过程数据 (已由从站更新) */
    memcpy(ctx->processdata_input, g_shm->esc.sm3_buffer,
           ctx->processdata_size);

    /* 提取 WKC */
    ec_u8 *resp = g_shm->esc.frame_out;
    ec_u16 resp_len = g_shm->esc.frame_out_len;
    if (resp_len >= 4) {
        ec_u16 wkc1 = resp[resp_len - 4] | ((ec_u16)resp[resp_len - 3] << 8);
        ec_u16 wkc2 = resp[resp_len - 2] | ((ec_u16)resp[resp_len - 1] << 8);
        g_working_counter = wkc1 + wkc2;
    }

    g_shm->esc.response_ready = 0;
    return 1;
}

// ==========================================================================
// 主函数
// ==========================================================================
int main(void) {
    signal(SIGINT, sigint_handler);
    printf("=== EtherCAT Master (SOEM-style) ===\n\n");

    // ---- 1. 打开共享内存 (连接虚拟从站) ----
    int shm_fd = shm_open(SHM_NAME, O_RDWR, 0666);
    if (shm_fd < 0) {
        fprintf(stderr, "[MASTER] ERROR: Cannot open shared memory '%s'\n", SHM_NAME);
        fprintf(stderr, "[MASTER] Please start ethercat_slave first!\n");
        return 1;
    }

    g_shm = (SlaveShm *)mmap(NULL, sizeof(SlaveShm),
                              PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (g_shm == MAP_FAILED) {
        perror("mmap");
        close(shm_fd);
        return 1;
    }
    printf("[MASTER] Connected to slave via shared memory\n\n");

    // ---- 2. 初始化主站 ----
    if (!ecx_init(&g_ctx)) {
        fprintf(stderr, "[MASTER] Init failed\n");
        return 1;
    }

    // ---- 3. 配置从站 ----
    EcSlave slave;
    memset(&slave, 0, sizeof(slave));

    if (!ecx_config_init(&g_ctx, &slave, 1)) return 1;
    if (!ecx_config_map(&g_ctx, &slave))    return 1;
    if (!ecx_config_dc(&g_ctx, &slave))     return 1;

    // 分配过程数据缓冲区
    ec_u8 pd_input[512], pd_output[512];
    g_ctx.processdata_input  = pd_input;
    g_ctx.processdata_output = pd_output;

    printf("\n");

    // ---- 4. 状态机推进 ----
    ecx_statecheck(&g_ctx, &slave, EC_STATE_INIT,   200);
    ecx_statecheck(&g_ctx, &slave, EC_STATE_PREOP,  200);
    ecx_statecheck(&g_ctx, &slave, EC_STATE_SAFEOP, 200);

    // SafeOp 阶段: 可以用 CoE SDO 读取映射信息
    printf("[MASTER] CoE SDO Read 0x1C13 (TxPDO assign)...\n");

    ecx_statecheck(&g_ctx, &slave, EC_STATE_OP, 200);
    printf("\n");

    // ---- 5. 实时循环 ----
    printf("[MASTER] Entering cyclic mode (1kHz)...\n");
    printf("[MASTER] ┌────────┬──────────┬──────────┬──────────┬──────────┬──────────┐\n");
    printf("[MASTER] │ cycle  │ axis0    │ axis1    │ axis2    │ wkc      │ dc_diff  │\n");
    printf("[MASTER] ├────────┼──────────┼──────────┼──────────┼──────────┼──────────┤\n");

    ec_u64 t_start = get_time_ns();
    ec_u64 t_next = t_start;
    int print_counter = 0;

    while (g_running && (get_time_ns() - t_start < SIM_DURATION_S * 1000000000ULL)) {
        // 等待下一个周期
        t_next += DC_CYCLE_NS;
        ec_i64 sleep_ns = (ec_i64)(t_next - get_time_ns());
        if (sleep_ns > 0) {
            struct timespec ts = {0, sleep_ns};
            nanosleep(&ts, NULL);
        }

        // 发送过程数据
        ecx_send_processdata(&g_ctx);

        // 接收过程数据 (模拟: 从站在共享内存中自动更新)
        ecx_receive_processdata(&g_ctx, 200);

        // 每 100ms 打印一次
        if (++print_counter >= 100) {
            print_counter = 0;
            ec_i32 *pos = (ec_i32 *)pd_input;
            ec_u16 *status = (ec_u16 *)(pd_input + 12);
            ec_i32 *targets = (ec_i32 *)pd_output;

            ec_i64 dc_diff = (ec_i64)(get_time_ns() - g_shm->esc.dc_time);

            printf("[MASTER] │ %6llu │ %8d │ %8d │ %8d │ 0x%04X   │ %+8lld │\n",
                   (unsigned long long)g_ctx.cycle_count,
                   pos[0], pos[1], pos[2],
                   g_working_counter, (long long)dc_diff);
        }
    }

    printf("[MASTER] └────────┴──────────┴──────────┴──────────┴──────────┴──────────┘\n");

    // ---- 6. 停止 ----
    printf("\n[MASTER] Shutting down...\n");
    ecx_statecheck(&g_ctx, &slave, EC_STATE_INIT, 200);

    munmap(g_shm, sizeof(SlaveShm));
    close(shm_fd);

    printf("[MASTER] Done. Total cycles: %llu\n",
           (unsigned long long)g_ctx.cycle_count);
    return 0;
}
