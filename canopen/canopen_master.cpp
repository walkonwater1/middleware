/*
 * canopen_master.cpp — CANopen 主站
 *
 * 功能:
 *   - NMT 命令: 启动/停止/复位从站
 *   - SDO 读: 读取从站对象字典
 *   - SDO 写: 配置从站参数 (目标位置)
 *   - RPDO 发射: 周期性发送目标位置给从站
 *   - PDO 接收: 订阅从站 TPDO1 并打印
 *   - Heartbeat 监控: 检测从站状态变化
 *   - EMCY 接收: 紧急报文处理
 *
 * 用法: ./canopen_master
 */

#include "canopen.hpp"
#include <chrono>
#include <csignal>
#include <cstdio>
#include <thread>

using namespace canopen;

static volatile bool g_running = true;
void sigint_handler(int) { g_running = false; }

int main() {
    printf("=== CANopen Master Starting ===\n");
    signal(SIGINT, sigint_handler);

    U8 slave_id = 1;
    VirtualCanBus bus;

    // ---- 1. 状态机 ----
    NmtStateMachine nmt(slave_id);

    // ---- 2. 本地区字典 (用于 RPDO 打包) ----
    ObjectDictionary local_od(0);
    local_od.add_entry({0x6040, 0, "Controlword",    OdAccessType::RW, OdValue(U16(0x000F))}); // Enable Op
    local_od.add_entry({0x607A, 0, "Target Position", OdAccessType::RW, OdValue(I32(0))});

    // RPDO1 映射 (对应从站的接收 PDO)
    PdoDescriptor rpdo1;
    rpdo1.comm.cob_id = COB_RPDO1(slave_id);
    rpdo1.comm.trans_type = 255;
    rpdo1.mapping = {
        {0x6040, 0, 16},
        {0x607A, 0, 32},
    };

    // ---- 3. 注册接收处理 ----
    // 3a. 接收 TPDO1 (从站周期性位置+速度)
    bus.subscribe(COB_TPDO1(slave_id), [&](const CanFrame& f) {
        if (f.dlc < 8) return;
        I32 pos, vel;
        memcpy(&pos, f.data, 4);
        memcpy(&vel, f.data + 4, 4);
        static int pdo1_cnt = 0;
        if (++pdo1_cnt % 100 == 0) {
            printf("[MASTER] PDO1 ← node#%d pos=%d vel=%d (x%d)\n",
                   slave_id, pos, vel, pdo1_cnt);
        }
    });

    // 3b. 接收 TPDO2
    bus.subscribe(COB_TPDO2(slave_id), [&](const CanFrame& f) {
        if (f.dlc < 4) return;
        I16 tor, cur;
        memcpy(&tor, f.data, 2);
        memcpy(&cur, f.data + 2, 2);
        static int pdo2_cnt = 0;
        if (++pdo2_cnt % 10 == 0) {
            printf("[MASTER] PDO2 ← node#%d torque=%d current=%d (x%d)\n",
                   slave_id, tor, cur, pdo2_cnt);
        }
    });

    // 3c. 接收 Heartbeat
    bus.subscribe(COB_HB(slave_id), [&](const CanFrame& f) {
        if (f.dlc < 1) return;
        NmtState s = static_cast<NmtState>(f.data[0]);
        static NmtState last_s = NmtState::BootUp;
        if (s != last_s) {
            printf("[MASTER] Heartbeat node#%d: %s\n", slave_id, nmt_state_str(s));
            last_s = s;
        }
    });

    // 3d. 接收 EMCY
    bus.subscribe(COB_EMCY(slave_id), [&](const CanFrame& f) {
        if (f.dlc < 8) return;
        U16 err = f.data[0] | (static_cast<U16>(f.data[1]) << 8);
        printf("[MASTER] EMCY ← node#%d code=0x%04X reg=0x%02X\n",
               slave_id, err, f.data[2]);
    });

    // ---- 4. 主站操作流程 ----
    auto log_and_sleep = [](const char* msg, int ms) {
        printf("[MASTER] %s\n", msg);
        std::this_thread::sleep_for(std::chrono::milliseconds(ms));
    };

    // Step 1: 先让从站有一段时间启动
    log_and_sleep("Waiting for slave bootup...", 200);

    // Step 2: 进入 Pre-Operational, 用 SDO 读取设备信息
    log_and_sleep("→ NMT Enter Pre-Operational", 100);
    bus.send(CanFrame::nmt(NmtCommand::EnterPreOp, slave_id));
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // SDO 读: 设备类型 (0x1000)
    bus.send(CanFrame(COB_RSDO(slave_id),
        []{
            U8 d[8] = {};
            d[0] = 0x40;           // Initiate Upload
            d[1] = 0x00; d[2] = 0x10;  // index = 0x1000
            d[3] = 0x00;           // sub = 0
            return d;
        }(), 8));
    log_and_sleep("SDO read Device Type (0x1000)", 50);

    // SDO 读: 厂商 ID
    bus.send(CanFrame(COB_RSDO(slave_id),
        []{
            U8 d[8] = {};
            d[0] = 0x40;
            d[1] = 0x18; d[2] = 0x10;  // 0x1018
            d[3] = 0x01;               // sub1: Vendor ID
            return d;
        }(), 8));
    log_and_sleep("SDO read Vendor ID (0x1018:01)", 50);

    // Step 3: 启动从站 → Operational
    log_and_sleep("→ NMT Start Remote Node", 100);
    bus.send(CanFrame::nmt(NmtCommand::StartRemoteNode, slave_id));
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Step 4: Operational 状态 — 周期性发送目标位置 (RPDO1)
    log_and_sleep("Operational: sending target position every 100ms...", 50);

    using Clock = std::chrono::steady_clock;
    auto t_start = Clock::now();
    int  target_pos = 0;

    while (g_running) {
        auto now = Clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - t_start).count();

        // 每 100ms 发送一次 RPDO1 (目标位置)
        static auto t_last_rpdo = t_start;
        auto elapsed_rpdo = std::chrono::duration_cast<std::chrono::milliseconds>(now - t_last_rpdo).count();

        if (elapsed_rpdo >= 100) {
            // 目标位置正弦变化
            target_pos = static_cast<int>(2048 * sin(elapsed * 0.001));
            auto* tgt = local_od.get_entry(0x607A, 0);
            if (tgt) tgt->value = OdValue(I32(target_pos));

            U8 buf[8] = {};
            size_t len = local_od.pdo_pack(rpdo1, buf, sizeof(buf));
            bus.send(CanFrame(COB_RPDO1(slave_id), buf, static_cast<U8>(len)));
            t_last_rpdo = now;
        }

        // 每 5 秒用 SDO 写入一次心跳时间 (验证 SDO 写)
        static auto t_last_sdo = t_start;
        auto elapsed_sdo = std::chrono::duration_cast<std::chrono::seconds>(now - t_last_sdo).count();
        if (elapsed_sdo >= 5) {
            U16 hb_time = 200; // 调整为 200ms
            U8 d[8] = {};
            d[0] = 0x2B;                           // Initiate Download (2 bytes)
            d[1] = 0x17; d[2] = 0x10;             // index = 0x1017
            d[3] = 0x00;                           // sub = 0
            memcpy(d + 4, &hb_time, 2);
            bus.send(CanFrame(COB_RSDO(slave_id), d, 6));
            printf("[MASTER] SDO write Heartbeat time → 200ms\n");
            t_last_sdo = now;
        }

        // 运行 30 秒后停止
        if (elapsed > 30000) {
            log_and_sleep("→ NMT Stop Remote Node", 100);
            bus.send(CanFrame::nmt(NmtCommand::StopRemoteNode, slave_id));
            break;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    printf("=== CANopen Master Stopped ===\n");
    return 0;
}
