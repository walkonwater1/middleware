/*
 * canopen_node.cpp — CANopen 从站节点
 *
 * 模拟一个 CiA 402 伺服驱动器从站:
 *   - TPDO1: 位置 + 速度 (1ms 周期)
 *   - TPDO2: 转矩 + 电流 (10ms 周期)
 *   - RPDO1: 目标位置 + 控制字
 *   - Heartbeat: 100ms
 *   - EMCY: 模拟过温告警
 *
 * 用法: ./canopen_node [node_id=1]
 */

#include "canopen.hpp"
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdio>
#include <thread>

using namespace canopen;

// 全局标志
static volatile bool g_running = true;
void sigint_handler(int) { g_running = false; }

int main(int argc, char* argv[]) {
    U8 node_id = (argc > 1) ? static_cast<U8>(std::atoi(argv[1])) : 1;
    printf("=== CANopen Node #%d Starting ===\n", node_id);

    signal(SIGINT, sigint_handler);

    // 1. 创建虚拟总线
    VirtualCanBus bus;

    // 2. 创建对象字典
    ObjectDictionary od(node_id);
    od.dump();

    // 3. 初始化 NMT 状态机
    NmtStateMachine nmt(node_id);

    // 4. 构建 PDO 描述符
    // TPDO1 映射: 位置(I32) + 速度(I32) = 8 字节
    PdoDescriptor tpdo1;
    tpdo1.comm.cob_id = COB_TPDO1(node_id);
    tpdo1.comm.trans_type = 255;
    tpdo1.comm.inhibit_time = 10;   // 100μs × 10 = 1ms
    tpdo1.mapping = {
        {0x6064, 0, 32},   // Actual Position (I32)
        {0x606C, 0, 32},   // Actual Velocity (I32)
    };

    // TPDO2 映射: 转矩(I16) + 电流(I16) = 4 字节
    PdoDescriptor tpdo2;
    tpdo2.comm.cob_id = COB_TPDO2(node_id);
    tpdo2.comm.trans_type = 254;  // 异步, 非同步
    tpdo2.comm.inhibit_time = 100; // 10ms
    tpdo2.mapping = {
        {0x6077, 0, 16},   // Actual Torque (I16)
        {0x6078, 0, 16},   // Actual Current (I16)
    };

    // RPDO1 映射: 控制字(U16) + 目标位置(I32) = 6 字节
    PdoDescriptor rpdo1;
    rpdo1.comm.cob_id = COB_RPDO1(node_id);
    rpdo1.comm.trans_type = 255;
    rpdo1.mapping = {
        {0x6040, 0, 16},   // Controlword (U16)
        {0x607A, 0, 32},   // Target Position (I32)
    };

    // 5. 注册 CAN 帧处理函数
    // 5a. NMT 命令
    bus.subscribe(COB_NMT, [&](const CanFrame& f) {
        if (f.dlc < 2) return;
        NmtCommand cmd = static_cast<NmtCommand>(f.data[0]);
        U8 target = f.data[1];
        if (target != 0 && target != node_id) return;  // 非本节点
        printf("[NODE#%d] NMT command: 0x%02X\n", node_id, static_cast<U8>(cmd));
        nmt.apply_command(cmd);
    });

    // 5b. RPDO1 — 接收主站发来的目标位置和控制字
    bus.subscribe(COB_RPDO1(node_id), [&](const CanFrame& f) {
        if (nmt.state() != NmtState::Operational) return;
        od.pdo_unpack(rpdo1, f.data, f.dlc);
        auto* ctrl = od.get_entry(0x6040, 0);
        auto* tgt  = od.get_entry(0x607A, 0);
        if (ctrl && tgt) {
            printf("[NODE#%d] RPDO1 ← ctrl=0x%04X target=%d\n",
                   node_id, ctrl->value.u16, tgt->value.i32);
        }
    });

    // 5c. SDO 请求 (通过 COB_RSDO, 客户端→服务端方向)
    //     此处简化: 在虚拟总线中直接响应
    bus.subscribe(COB_RSDO(node_id), [&](const CanFrame& f) {
        if (nmt.state() == NmtState::Stopped) return;

        U8 sdo_cs = f.data[0];  // 命令说明符
        bool is_read = (sdo_cs == 0x40);    // Initiate Upload
        bool is_write = (sdo_cs == 0x22 || sdo_cs == 0x23 || sdo_cs == 0x21); // Initiate Download

        if (is_read) {
            U16 index = f.data[1] | (static_cast<U16>(f.data[2]) << 8);
            U8  sub   = f.data[3];
            U8  buf[4] = {};
            size_t len = 4;
            if (od.sdo_read(index, sub, buf, &len)) {
                printf("[NODE#%d] SDO read  [%04X:%02X] OK\n", node_id, index, sub);
                // 正常应回复 SDO 响应帧，此处简化
            } else {
                printf("[NODE#%d] SDO read  [%04X:%02X] FAIL (no entry)\n", node_id, index, sub);
            }
        } else if (is_write) {
            U16 index = f.data[1] | (static_cast<U16>(f.data[2]) << 8);
            U8  sub   = f.data[3];
            U32 abort = 0;
            if (od.sdo_write(index, sub, f.data + 4, f.dlc - 4, &abort)) {
                printf("[NODE#%d] SDO write [%04X:%02X] OK\n", node_id, index, sub);
            } else {
                printf("[NODE#%d] SDO write [%04X:%02X] FAIL (abort=0x%08X)\n", node_id, index, sub, abort);
            }
        }
    });

    // 6. 启动节点
    nmt.boot_up();

    // 7. 主循环: 模拟传感器数据并定期发送 PDO / Heartbeat / EMCY
    using Clock = std::chrono::steady_clock;
    auto t_start     = Clock::now();
    auto t_last_hb   = t_start;
    auto t_last_pdo1 = t_start;
    auto t_last_pdo2 = t_start;
    auto t_last_emcy = t_start;

    int loop_cnt = 0;

    while (g_running) {
        auto now = Clock::now();
        auto elapsed_hb = std::chrono::duration_cast<std::chrono::milliseconds>(now - t_last_hb).count();
        auto elapsed_pdo1 = std::chrono::duration_cast<std::chrono::milliseconds>(now - t_last_pdo1).count();

        // ---- 更新模拟传感器值 ----
        if (nmt.state() == NmtState::Operational) {
            // 模拟正弦波运动
            int pos = static_cast<int>(1024 * sin(loop_cnt * 0.05));
            int vel = static_cast<int>(300  * cos(loop_cnt * 0.05));
            I16 tor = static_cast<I16>(50 * sin(loop_cnt * 0.03));
            I16 cur = static_cast<I16>(120 + 10 * cos(loop_cnt * 0.03));

            od.get_entry(0x6064, 0)->value = OdValue(I32(pos));
            od.get_entry(0x606C, 0)->value = OdValue(I32(vel));
            od.get_entry(0x6077, 0)->value = OdValue(tor);
            od.get_entry(0x6078, 0)->value = OdValue(cur);

            // 更新状态字
            od.get_entry(0x6041, 0)->value = OdValue(U16(0x0237)); // Operation Enabled
        }

        // ---- TPDO1 周期发送 (1ms) ----
        if (nmt.state() == NmtState::Operational && elapsed_pdo1 >= 10) {
            U8 buf[8] = {};
            size_t len = od.pdo_pack(tpdo1, buf, sizeof(buf));
            bus.send(CanFrame::tpdo1(node_id, buf, static_cast<U8>(len)));
            t_last_pdo1 = now;

            auto* pos = od.get_entry(0x6064, 0);
            auto* vel = od.get_entry(0x606C, 0);
            if (loop_cnt % 10 == 0 && pos && vel) {
                printf("[NODE#%d] TPDO1 → pos=%-6d vel=%-5d\n",
                       node_id, pos->value.i32, vel->value.i32);
            }
        }

        // ---- TPDO2 周期发送 (10ms) ----
        auto elapsed_pdo2 = std::chrono::duration_cast<std::chrono::milliseconds>(now - t_last_pdo2).count();
        if (nmt.state() == NmtState::Operational && elapsed_pdo2 >= 100) {
            U8 buf[8] = {};
            size_t len = od.pdo_pack(tpdo2, buf, sizeof(buf));
            bus.send(CanFrame::tpdo2(node_id, buf, static_cast<U8>(len)));
            t_last_pdo2 = now;
        }

        // ---- Heartbeat 周期发送 ----
        if (elapsed_hb >= od.heartbeat_producer_time()) {
            bus.send(CanFrame::heartbeat(node_id, nmt.state()));
            t_last_hb = now;
        }

        // ---- 模拟 EMCY: 每 5 秒触发一次过温告警 ----
        auto elapsed_emcy = std::chrono::duration_cast<std::chrono::milliseconds>(now - t_last_emcy).count();
        if (nmt.state() == NmtState::Operational && elapsed_emcy >= 5000) {
            EmcyMessage em;
            em.error_code     = EmcyCode::TEMPERATURE + 2;  // 过温
            em.error_register = 0x01;                       // generic error
            em.mf_specific[0] = node_id;
            em.mf_specific[1] = 85;  // 当前温度 ℃
            bus.send(CanFrame::emcy(node_id, em));
            printf("[NODE#%d] EMCY → TEMPERATURE: 85°C\n", node_id);
            t_last_emcy = now;
        }

        loop_cnt++;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    printf("=== CANopen Node #%d Stopped ===\n", node_id);
    return 0;
}
