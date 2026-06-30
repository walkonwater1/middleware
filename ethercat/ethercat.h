/*
 * ethercat.h — EtherCAT 核心定义
 *
 * 参考: Beckhoff ETG.1000, ET1100/ET1200 ESC 手册
 *
 * 本文件定义了 EtherCAT 协议栈的核心结构:
 *   - ESC 寄存器布局 (物理地址映射)
 *   - EtherCAT 状态机 (ESM)
 *   - SyncManager / FMMU 配置
 *   - PDO 映射 (CoE)
 *   - 分布式时钟 (DC) 数据结构
 *   - 数据报和过程数据帧
 */

#ifndef ETHERCAT_H
#define ETHERCAT_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// ==========================================================================
// 基本类型
// ==========================================================================
typedef uint8_t   ec_u8;
typedef uint16_t  ec_u16;
typedef uint32_t  ec_u32;
typedef uint64_t  ec_u64;
typedef int32_t   ec_i32;
typedef int64_t   ec_i64;

// ==========================================================================
// ESC 寄存器地址 (物理地址, 偏移于 ESC 基址)
// ==========================================================================
#define ESC_REG_TYPE           0x0000   // ESC 类型
#define ESC_REG_REVISION       0x0001   // 版本
#define ESC_REG_BUILD          0x0002   // 构建号
#define ESC_REG_FMMU_COUNT     0x0004   // FMMU 数量
#define ESC_REG_SM_COUNT       0x0005   // SyncManager 数量
#define ESC_REG_RAM_SIZE       0x0006   // DPRAM 大小 (KB)
#define ESC_REG_PORT_DESC      0x0007   // 端口描述
#define ESC_REG_ESC_FEATURES   0x0008   // ESC 特性

#define ESC_REG_DL_STATUS      0x0110   // 数据链路状态
#define ESC_REG_AL_CONTROL     0x0120   // 应用层控制 (写入以改变状态)
#define ESC_REG_AL_STATUS      0x0130   // 应用层状态 (读取当前状态)
#define ESC_REG_AL_STATUS_CODE 0x0134   // 状态码
#define ESC_REG_AL_EVENT       0x0220   // 应用层事件请求

#define ESC_REG_WDOG_DIV       0x0400   // 看门狗分频器
#define ESC_REG_WDOG_PDI       0x0410   // 看门狗 PDI

#define ESC_REG_SM(n)          (0x0800 + (n) * 8)   // SyncManager 基址
#define ESC_REG_SM_PHY_START   0x00     // + SM物理起始地址
#define ESC_REG_SM_LENGTH      0x02     // + SM长度
#define ESC_REG_SM_CTRL        0x04     // + SM控制寄存器
#define ESC_REG_SM_STATUS      0x05     // + SM状态寄存器

#define ESC_REG_FMMU(n)        (0x0600 + (n) * 16)  // FMMU 基址

#define ESC_REG_DC_RECV_TIME   0x0900   // DC 接收时间 (32位)
#define ESC_REG_DC_SYSTIME     0x0910   // DC 系统时间 (64位)
#define ESC_REG_DC_SPEED_CNT   0x0930   // DC 速度计数器
#define ESC_REG_DC_SYNC_ACT    0x0981   // DC Sync 激活
#define ESC_REG_DC_SYNC0_CYC   0x09A0   // Sync0 周期时间
#define ESC_REG_DC_SYNC1_CYC   0x09A4   // Sync1 周期时间

// ESC 寄存器空间大小
#define ESC_MEM_SIZE            0x1000

// ==========================================================================
// EtherCAT 状态机 (ESM) — AL 控制/状态寄存器
// ==========================================================================
typedef enum {
    EC_STATE_INIT    = 0x01,
    EC_STATE_PREOP   = 0x02,
    EC_STATE_BOOT    = 0x03,   // Bootstrap (可选, 固件更新)
    EC_STATE_SAFEOP  = 0x04,
    EC_STATE_OP      = 0x08
} EcState;

#define EC_STATE_MASK 0x0F

// AL 控制寄存器请求位
#define EC_AL_CTRL_ACK    0x0010   // 应答位
#define EC_AL_CTRL_ERROR  0x0010   // ERROR 指示 (读取用)

const char* ec_state_str(EcState s);

// ==========================================================================
// EtherCAT 数据报命令
// ==========================================================================
typedef enum {
    EC_CMD_NOP      = 0x00,   // 空操作
    EC_CMD_APRD     = 0x01,   // 自增位置读
    EC_CMD_APWR     = 0x02,   // 自增位置写
    EC_CMD_APRW     = 0x03,   // 自增位置读写
    EC_CMD_FPRD     = 0x04,   // 固定地址读
    EC_CMD_FPWR     = 0x05,   // 固定地址写
    EC_CMD_FPRW     = 0x06,   // 固定地址读写
    EC_CMD_BRD      = 0x07,   // 广播读
    EC_CMD_BWR      = 0x08,   // 广播写
    EC_CMD_LRD      = 0x0A,   // 逻辑读
    EC_CMD_LWR      = 0x0B,   // 逻辑写
    EC_CMD_LRW      = 0x0C,   // 逻辑读写
    EC_CMD_ARMW     = 0x0D,   // 自增位置读写多帧 (用于DC)
    EC_CMD_FRMW     = 0x0E,   // 固定地址读写多帧
} EcCmd;

// ==========================================================================
// SyncManager 类型
// ==========================================================================
typedef enum {
    SM_TYPE_UNUSED   = 0,
    SM_TYPE_MAILBOX  = 1,    // 邮箱通信 (CoE/FoE/EoE/SoE)
    SM_TYPE_INPUT    = 2,    // 过程数据输入
    SM_TYPE_OUTPUT   = 3,    // 过程数据输出
    SM_TYPE_BUFFERED_INPUT  = 4,
    SM_TYPE_BUFFERED_OUTPUT = 4,
} SmType;

// SyncManager 控制寄存器
#define SM_CTRL_ENABLE    0x01
#define SM_CTRL_REPEAT    0x02
#define SM_CTRL_IRQ       0x04
#define SM_CTRL_OPMODE(x) (((x) & 3) << 4)   // SM类型

// ==========================================================================
// CoE (CANopen over EtherCAT) — 邮箱协议
// ==========================================================================
#define COE_SDO_REQ_HEADER_SIZE  10

typedef enum {
    COE_REQ_DOWNLOAD     = 2,    // SDO 下载请求
    COE_REQ_UPLOAD       = 3,    // SDO 上传请求
    COE_RES_DOWNLOAD     = 3,    // SDO 下载响应
    COE_RES_UPLOAD       = 2,    // SDO 上传响应
    COE_EMERGENCY        = 4,    // 紧急报文
} CoEMbxType;

// CoE SDO 请求头
typedef struct __attribute__((packed)) {
    ec_u16  index;         // 对象字典索引
    ec_u8   sub_index;     // 子索引
    ec_u32  complete_access : 1;
    ec_u32  reserved        : 31;
} CoESdoHeader;

// CoE SDO 信息区
typedef enum {
    COE_CHECK_OD_LIST         = 1,
    COE_CHECK_OBJECT           = 2,
    COE_CHECK_ENTRY_DESC       = 3,
    COE_CHECK_SDO_INFO         = 4,
    COE_CHECK_PDO_ASSIGN       = 0x10,
    COE_CHECK_PDO_MAPPING      = 0x11,
} CoEInfoOp;

// 标准 CoE 对象 (与 CANopen 共享对象字典空间)
// 通信参数
#define COE_INDEX_RXPDO_ASSIGN    0x1C12   // RxPDO 分配
#define COE_INDEX_TXPDO_ASSIGN    0x1C13   // TxPDO 分配
#define COE_INDEX_RXPDO_MAP(n)    (0x1600 + (n))  // RxPDO 映射
#define COE_INDEX_TXPDO_MAP(n)    (0x1A00 + (n))  // TxPDO 映射
#define COE_INDEX_SYNC_MANAGER_CH 0x1C32   // SM 输出参数
#define COE_INDEX_SYNC_MANAGER_CO 0x1C33   // SM 输入参数
#define COE_INDEX_DC_CYCLE_TIME    0x1C32:0x02
#define COE_INDEX_DC_SYNC0_CYC    0x1C32:0x03

// CoE 对象字典值
typedef struct {
    ec_u16 index;
    ec_u8  sub_index;
    ec_u8  bit_length;
    ec_u32 value;
} CoeOdEntry;

// ==========================================================================
// PDO 描述符
// ==========================================================================
typedef struct {
    ec_u16 index;         // 对象字典索引
    ec_u8  sub_index;     // 子索引
    ec_u8  bit_length;    // 位长 (8/16/32)
} PdoEntry;

typedef struct {
    ec_u16     sm;               // 所属 SyncManager
    ec_u8      num_entries;      // 映射条目数
    PdoEntry   entries[8];       // 最多 8 个条目 (实际由 SM 长度限制)
    ec_u16     total_bit_length; // 总位长
} PdoMapping;

// ==========================================================================
// 分布式时钟 (DC) 配置
// ==========================================================================
typedef struct {
    ec_u32  sync0_cycle_ns;      // Sync0 周期 (ns), 典型值 1ms = 1e6
    ec_u32  sync0_shift_ns;      // Sync0 偏移 (ns), 提前量
    ec_u32  sync1_cycle_ns;      // Sync1 周期
    ec_u32  sync1_shift_ns;      // Sync1 偏移
    ec_u8   activate;            // 激活位: bit0=Sync0, bit1=Sync1
} DcConfig;

// DC 漂移相关
typedef struct {
    ec_i32  drift_ns;            // 当前漂移 (ns)
    ec_i32  drift_comp_ns;       // 补偿量
    ec_u64  local_time;          // 本地时间
    ec_u64  master_time;         // 主站参考时间
    ec_i64  offset;              // 与主站的偏移
    int     dc_supported;        // 是否支持 DC
} DcStat;

// ==========================================================================
// 从站信息 (类似 SOEM 的 ec_slavet)
// ==========================================================================
typedef struct {
    // 基本标识
    int      alias;              // 别名
    int      position;           // 总线位置
    ec_u32   vendor_id;          // 厂商 ID
    ec_u32   product_code;       // 产品代码
    ec_u32   revision;           // 版本
    ec_u32   serial;             // 序列号

    // 状态
    EcState   state;             // 当前状态
    EcState   requested_state;   // 请求状态
    int       al_error;          // 应用层错误

    // SyncManager 配置
    ec_u8     sm_count;
    ec_u16    sm_laddr[16];      // 逻辑地址
    ec_u16    sm_size[16];       // SM 大小
    ec_u8     sm_type[16];       // SM 类型

    // FMMU 配置
    ec_u8     fmmu_count;

    // PDO 映射
    PdoMapping rx_pdo[8];        // 接收 PDO (主→从)
    PdoMapping tx_pdo[8];        // 发送 PDO (从→主)

    // 过程数据指针 (映射到共享内存)
    ec_u8     *inputs;           // 输入数据缓存
    ec_u8     *outputs;          // 输出数据缓存
    ec_u16    input_size;        // 输入总字节
    ec_u16    output_size;       // 输出总字节

    // DC 状态
    DcStat    dc;

    // 工作计数器 (Working Counter)
    ec_u16    wkc;
} EcSlave;

// ==========================================================================
// 虚拟 ESC (用于无硬件仿真)
// ==========================================================================
typedef struct {
    // ESC 寄存器空间
    ec_u8    memory[ESC_MEM_SIZE];

    // 过程数据 SM2 (输出, 主→从) 和 SM3 (输入, 从→主)
    ec_u8    sm2_buffer[512];
    ec_u8    sm3_buffer[512];
    ec_u16   sm2_size;
    ec_u16   sm3_size;

    // 模拟的伺服轴数据 (3轴)
    ec_i32   axis_actual_pos[3];    // 实际位置 (编码器反馈)
    ec_i32   axis_target_pos[3];    // 目标位置 (来自主站)
    ec_i32   axis_actual_vel[3];
    ec_i32   axis_actual_torque[3];

    // DC 时间
    ec_u64   dc_time;
    ec_u64   dc_speed_cnt_per_sec;

    // 状态
    EcState  state;
    ec_u16   wkc;
    int      position;

    // 数据报帧缓冲区 (shared memory IPC)
    ec_u8    frame_in[2048];      /* 主站 → 从站: 待处理数据报 */
    ec_u16   frame_in_len;
    ec_u8    frame_ready;         /* 主站置1表示帧就绪, 从站处理后置0 */
    ec_u8    frame_out[2048];     /* 从站 → 主站: 处理后的响应 */
    ec_u16   frame_out_len;
    ec_u8    response_ready;      /* 从站置1表示响应就绪 */
} VirtualEsc;

// ==========================================================================
// 主站上下文 (类似 SOEM 的 ecx_contextt)
// ==========================================================================
typedef struct {
    EcSlave   *slaves;
    int        slave_count;

    // DC 配置
    DcConfig   dc_config;
    int        dc_active;

    // 周期统计
    ec_u64     cycle_count;
    ec_i64     cycle_time_ns;

    // 过程数据
    ec_u8     *processdata_input;
    ec_u8     *processdata_output;
    ec_u16     processdata_size;
} EcxContext;

// ==========================================================================
// API: 虚拟 ESC 操作
// ==========================================================================
void virtual_esc_init(VirtualEsc *esc, int position);
void virtual_esc_update(VirtualEsc *esc);
void virtual_esc_process_frame(VirtualEsc *esc,
                                ec_u8 *frame, ec_u16 frame_len,
                                ec_u8 *response, ec_u16 *response_len);

// ==========================================================================
// API: 主站 (SOEM 风格)
// ==========================================================================
int  ecx_init(EcxContext *ctx);
int  ecx_config_init(EcxContext *ctx, EcSlave *slave, int slave_count);
int  ecx_config_map(EcxContext *ctx, EcSlave *slave);
int  ecx_config_dc(EcxContext *ctx, EcSlave *slave);
int  ecx_statecheck(EcxContext *ctx, EcSlave *slave,
                     EcState req_state, int timeout_ms);
int  ecx_send_processdata(EcxContext *ctx);
int  ecx_receive_processdata(EcxContext *ctx, int timeout_ms);

// CoE SDO 读写
int  ecx_coe_sdo_read(EcxContext *ctx, EcSlave *slave,
                       ec_u16 index, ec_u8 sub_index,
                       void *data, int *data_len);
int  ecx_coe_sdo_write(EcxContext *ctx, EcSlave *slave,
                        ec_u16 index, ec_u8 sub_index,
                        const void *data, int data_len);

// ==========================================================================
// State 字符串
// ==========================================================================
const char* ec_state_str(EcState s);

#ifdef __cplusplus
}
#endif

#endif // ETHERCAT_H
