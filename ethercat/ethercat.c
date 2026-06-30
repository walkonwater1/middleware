/**
 * ethercat.c — EtherCAT 核心函数实现
 *
 * 补充 ethercat.h 中声明但尚未实现的函数:
 *   - virtual_esc_process_frame() : 数据报处理核心
 *   - ecx_coe_sdo_read/write()    : CoE SDO 邮箱协议
 *   - ec_state_str()              : 状态字符串
 */

#include "ethercat.h"
#include <string.h>
#include <stdio.h>

/* ============================================================
 * 状态字符串
 * ============================================================ */
const char* ec_state_str(EcState s) {
    switch (s) {
        case EC_STATE_INIT:   return "INIT";
        case EC_STATE_PREOP:  return "PREOP";
        case EC_STATE_BOOT:   return "BOOT";
        case EC_STATE_SAFEOP: return "SAFEOP";
        case EC_STATE_OP:     return "OP";
        default:              return "UNKNOWN";
    }
}

/* ============================================================
 * ESC 寄存器读写辅助
 * ============================================================ */
static ec_u16 esc_read_u16(VirtualEsc *esc, ec_u16 addr) {
    if (addr + 1 >= ESC_MEM_SIZE) return 0;
    return (ec_u16)esc->memory[addr] | ((ec_u16)esc->memory[addr + 1] << 8);
}

static ec_u32 esc_read_u32(VirtualEsc *esc, ec_u16 addr) {
    if (addr + 3 >= ESC_MEM_SIZE) return 0;
    return (ec_u32)esc->memory[addr]
         | ((ec_u32)esc->memory[addr + 1] << 8)
         | ((ec_u32)esc->memory[addr + 2] << 16)
         | ((ec_u32)esc->memory[addr + 3] << 24);
}

static void esc_write_u16(VirtualEsc *esc, ec_u16 addr, ec_u16 val) {
    if (addr + 1 >= ESC_MEM_SIZE) return;
    esc->memory[addr]     = (ec_u8)(val & 0xFF);
    esc->memory[addr + 1] = (ec_u8)((val >> 8) & 0xFF);
}

static void esc_write_u32(VirtualEsc *esc, ec_u16 addr, ec_u32 val) {
    if (addr + 3 >= ESC_MEM_SIZE) return;
    esc->memory[addr]     = (ec_u8)(val & 0xFF);
    esc->memory[addr + 1] = (ec_u8)((val >> 8) & 0xFF);
    esc->memory[addr + 2] = (ec_u8)((val >> 16) & 0xFF);
    esc->memory[addr + 3] = (ec_u8)((val >> 24) & 0xFF);
}

/* ============================================================
 * virtual_esc_process_frame() — EtherCAT 数据报处理核心
 *
 * 数据报格式 (简化):
 *   每个数据报: Cmd(1) | Idx(1) | Address(4) | Len(2) | Rsvd(1) | More(1)
 *               = 10 bytes header
 *               后跟 Data[Len] + WKC(2)
 *
 * 处理的命令:
 *   BRD: 广播读 — 读 ESC 寄存器
 *   BWR: 广播写 — 写 ESC 寄存器
 *   APRD/FPRD: 读 — 读 ESC 寄存器/SM 缓冲区
 *   APWR/FPWR: 写 — 写 ESC 寄存器/SM 缓冲区
 *   LRD: 逻辑读 — 通过 FMMU 映射后读取
 *   LWR: 逻辑写 — 通过 FMMU 映射后写入
 * ============================================================ */
void virtual_esc_process_frame(VirtualEsc *esc,
                                ec_u8 *frame, ec_u16 frame_len,
                                ec_u8 *response, ec_u16 *response_len)
{
    *response_len = 0;
    ec_u16 offset = 0;
    int datagram_count = 0;

    while (offset + 10 <= frame_len) {
        ec_u8  cmd      = frame[offset];
        ec_u8  idx      = frame[offset + 1];
        ec_u32 address  = (ec_u32)frame[offset + 2]
                        | ((ec_u32)frame[offset + 3] << 8)
                        | ((ec_u32)frame[offset + 4] << 16)
                        | ((ec_u32)frame[offset + 5] << 24);
        ec_u16 data_len = (ec_u16)frame[offset + 6]
                        | ((ec_u16)frame[offset + 7] << 8);
        ec_u8  more     = frame[offset + 9];  /* 是否还有后续数据报 */

        /* 简化的地址: 低16位 = 寄存器偏移, 高16位 = 区域选择
         * 0x0000xxxx = ESC 寄存器空间
         * 0x10000000 + n = SM mailbox buffer n
         * 0x10000000 + 2 = SM2 output buffer (过程数据输出)
         * 0x10000000 + 3 = SM3 input buffer  (过程数据输入)
         */

        ec_u16 reg_addr = (ec_u16)(address & 0xFFFF);
        ec_u16 region    = (ec_u16)((address >> 16) & 0xFFFF);
        ec_u16 wkc       = 0;  /* Working Counter — 成功处理的子操作数 */

        // printf("  [ESC] datagram #%d: cmd=0x%02X addr=0x%08X len=%d\n",
        //        datagram_count, cmd, address, data_len);

        switch (cmd) {
        case EC_CMD_NOP:
            wkc = 1; /* NOP always succeeds */
            break;

        case EC_CMD_BRD:   /* 广播读 */
        case EC_CMD_FPRD:  /* 固定地址读 */
        case EC_CMD_APRD:  /* 自增地址读 */
        case EC_CMD_LRD:   /* 逻辑读 */
        {
            if (region == 0x1000) {
                /* SM buffer 读 */
                int sm = reg_addr;
                ec_u8 *buf = NULL;
                ec_u16 buf_size = 0;
                if (sm == 0 || sm == 1) { buf = NULL; buf_size = 0; }  /* mailbox */
                else if (sm == 2) { buf = esc->sm2_buffer; buf_size = esc->sm2_size; }
                else if (sm == 3) { buf = esc->sm3_buffer; buf_size = esc->sm3_size; }

                if (buf && data_len <= buf_size) {
                    memcpy(response + *response_len, buf, data_len);
                    *response_len += data_len;
                    wkc = 1;
                }
            } else {
                /* ESC 寄存器读 */
                if (reg_addr + data_len <= ESC_MEM_SIZE) {
                    memcpy(response + *response_len, esc->memory + reg_addr, data_len);
                    *response_len += data_len;
                    wkc = 1;
                }
            }
            break;
        }

        case EC_CMD_BWR:   /* 广播写 */
        case EC_CMD_FPWR:  /* 固定地址写 */
        case EC_CMD_APWR:  /* 自增地址写 */
        case EC_CMD_LWR:   /* 逻辑写 */
        {
            ec_u8 *src_data = frame + offset + 10;
            if (region == 0x1000) {
                /* SM buffer 写 */
                int sm = reg_addr;
                if (sm == 2 && data_len <= 512) {
                    memcpy(esc->sm2_buffer, src_data, data_len);
                    esc->sm2_size = data_len;
                    wkc = 1;

                    /* 如果是 OP 状态, 解析目标位置 */
                    if (esc->state == EC_STATE_OP && data_len >= 12) {
                        memcpy(&esc->axis_target_pos[0], src_data, 4);
                        memcpy(&esc->axis_target_pos[1], src_data + 4, 4);
                        memcpy(&esc->axis_target_pos[2], src_data + 8, 4);
                    }
                }
                /* SM0 写 (mailbox output) */
                else if (sm == 0 && data_len <= 512) {
                    memcpy(esc->sm2_buffer, src_data, data_len); /* 复用 sm2_buffer */
                    wkc = 1;
                }
            } else {
                /* ESC 寄存器写 */
                if (reg_addr + data_len <= ESC_MEM_SIZE) {
                    memcpy(esc->memory + reg_addr, src_data, data_len);

                    /* AL 控制寄存器写入 → 触发状态转换 */
                    if (reg_addr <= ESC_REG_AL_CONTROL &&
                        reg_addr + data_len > ESC_REG_AL_CONTROL) {
                        ec_u16 al_ctrl = esc_read_u16(esc, ESC_REG_AL_CONTROL);
                        EcState req = (EcState)(al_ctrl & 0x0F);
                        if (req != esc->state) {
                            printf("  [ESC] 状态转换: %s → %s\n",
                                   ec_state_str(esc->state), ec_state_str(req));
                            esc->state = req;
                            /* 设置 ACK 位 */
                            ec_u16 al_status = (ec_u16)req | EC_AL_CTRL_ACK;
                            esc_write_u16(esc, ESC_REG_AL_STATUS, al_status);
                        }
                    }
                    wkc = 1;
                }
            }
            break;
        }

        default:
            /* 不支持的命令 — WKC=0 (不会被计数) */
            break;
        }

        /* 在响应帧末尾追加 WKC */
        response[*response_len]     = (ec_u8)(wkc & 0xFF);
        response[*response_len + 1] = (ec_u8)((wkc >> 8) & 0xFF);
        *response_len += 2;

        /* 更新 ESC 内部 WKC (用于统计) */
        esc->wkc += wkc;

        datagram_count++;

        /* 移动到下一个数据报 */
        offset += 10 + data_len;
        if (!more) break;
    }
}

/* ============================================================
 * ecx_coe_sdo_read() — CoE SDO 邮箱上传 (Upload)
 *
 * 构造 CoE SDO Upload 请求并写入 SM0 (mailbox output),
 * 然后从 SM1 读取响应.
 * ============================================================ */
int ecx_coe_sdo_read(EcxContext *ctx, EcSlave *slave,
                      ec_u16 index, ec_u8 sub_index,
                      void *data, int *data_len)
{
    (void)ctx;
    printf("[CoE] SDO Upload 0x%04X:%02X (requesting %d bytes)\n",
           index, sub_index, *data_len);

    /* 简化实现: 直接从共享内存的 SM3 缓冲区中查找 CoE 对象 */
    /* 在真实的 EtherCAT 中, 这会通过邮箱协议完成 */

    /* 模拟: 返回部分对象字典值 */
    ec_u32 val = 0;
    int found = 1;

    switch (index) {
        case 0x1000: val = 0x00020192; break;  /* CiA 402 servo drive */
        case 0x1001: val = 0x00000000; break;  /* Error register */
        case 0x1008: val = 0x454C3635; break;  /* "EL65" (simplified) */
        case 0x1009: val = 0x32314342; break;  /* "21CB" */
        case 0x1018:
            switch (sub_index) {
                case 1:  val = slave->vendor_id; break;
                case 2:  val = slave->product_code; break;
                case 3:  val = slave->revision; break;
                case 4:  val = slave->serial; break;
                default: found = 0; break;
            }
            break;
        case 0x1C12: val = 0x1600; break;  /* RxPDO assign */
        case 0x1C13: val = 0x1A00; break;  /* TxPDO assign */
        case 0x1C32: val = 3000000; break;  /* DC cycle 3ms */
        case 0x1C33: val = 3000000; break;
        default: found = 0; break;
    }

    if (found) {
        memcpy(data, &val, (*data_len < 4) ? *data_len : 4);
        *data_len = 4;
        return 1; /* OK */
    } else {
        *data_len = 0;
        return 0; /* Not found */
    }
}

/* ============================================================
 * ecx_coe_sdo_write() — CoE SDO 邮箱下载 (Download)
 * ============================================================ */
int ecx_coe_sdo_write(EcxContext *ctx, EcSlave *slave,
                       ec_u16 index, ec_u8 sub_index,
                       const void *data, int data_len)
{
    (void)ctx;
    (void)slave;

    ec_u32 val = 0;
    memcpy(&val, data, (data_len < 4) ? data_len : 4);

    printf("[CoE] SDO Download 0x%04X:%02X = 0x%08X (%d bytes)\n",
           index, sub_index, val, data_len);

    /* 简化: 对于 demo 来说, SDO write 总是返回成功 */
    /* 在实际系统中会通过 SM0/SM1 邮箱通道完成 */
    return 1;
}
