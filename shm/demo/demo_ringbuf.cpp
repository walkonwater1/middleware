/**
 * demo_ringbuf.cpp — 使用 ShmRingBuf<T> 实现机器人关节指令队列
 *
 * 场景: 规划器(producer) 下发 6 轴关节角度 → 控制器(consumer) 逐帧读取执行
 *
 * 对比原 shm_sem_producer.c / shm_sem_consumer.c:
 *   - 不需要手动管理信号量
 *   - 类型安全的 ShmRingBuf<JointCmd, 256>, write/read 就是 push/pop
 *   - RAII 自动清理共享内存
 *
 * 用法:
 *   ./demo_ringbuf producer    # 终端 1 — 连续写入关节指令
 *   ./demo_ringbuf consumer    # 终端 2 — 读取并模拟执行
 */

#include <iostream>
#include <iomanip>
#include <cstring>
#include <csignal>
#include <chrono>
#include <thread>
#include <atomic>
#include <cmath>

#include "shm_cxx.hpp"

using namespace shm;

// ============================================================
// 数据结构
// ============================================================
struct JointCmd {
    uint64_t sequence   = 0;
    uint64_t timestamp  = 0;
    double   positions[6] = {};   // 6 轴目标角度 (rad)
    double   duration_ms = 0;     // 本次运动时长
    char     label[32]   = {};    // 标签
};

constexpr const char* SHM_NAME = "/robot_joint_queue";

// ============================================================
// 信号
// ============================================================
static std::atomic<bool> running{true};
void on_signal(int) { running = false; }

// ============================================================
// Producer — 模拟轨迹规划器
// ============================================================
static int run_producer() {
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    // ========== 一行创建环形缓冲区 ==========
    ShmRingBuf<JointCmd, 256> ring(SHM_NAME, Create);

    std::cout << "[PRODUCER] 环形缓冲区已创建: " << SHM_NAME << "\n"
              << "[PRODUCER] 容量: 256 条  |  每条 " << sizeof(JointCmd) << " bytes\n"
              << "[PRODUCER] 总大小: " << ringbuf_total_size(256, sizeof(JointCmd)) << " bytes\n"
              << "[PRODUCER] 开始写入关节轨迹 (20Hz, Ctrl-C 退出)...\n"
              << std::endl;

    uint64_t seq = 0;
    int dropped = 0;

    while (running) {
        JointCmd cmd;
        cmd.sequence    = ++seq;
        cmd.duration_ms = 50.0;   // 每帧 50ms = 20Hz

        using namespace std::chrono;
        cmd.timestamp = duration_cast<microseconds>(
            system_clock::now().time_since_epoch()).count();

        // 生成正弦轨迹 (模拟 6 轴机械臂运动)
        double t = seq * 0.05;  // 50ms per frame
        for (int i = 0; i < 6; i++) {
            cmd.positions[i] = sin(t * 0.5 + i * 0.6) * 1.5;
        }
        snprintf(cmd.label, sizeof(cmd.label), "traj_%06lu", seq);

        if (ring.write(cmd) == 0) {
            std::cout << std::fixed << std::setprecision(2)
                      << "[PROD] #" << std::setw(5) << seq
                      << " | q=[" << cmd.positions[0]
                      << " " << cmd.positions[1]
                      << " " << cmd.positions[2]
                      << " " << cmd.positions[3]
                      << " " << cmd.positions[4]
                      << " " << cmd.positions[5] << "]"
                      << " | buf=" << ring.count() << "/256"
                      << std::endl;
        } else {
            dropped++;
            std::cout << "[PROD] #" << std::setw(5) << seq
                      << " ⚠ DROP (缓冲区满, 共丢 " << dropped << " 帧)"
                      << std::endl;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    std::cout << "\n[PRODUCER] 停止, 共写入 " << seq
              << " 帧, 丢弃 " << dropped << " 帧" << std::endl;
    return 0;
}

// ============================================================
// Consumer — 模拟运动控制器
// ============================================================
static int run_consumer() {
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    // ========== 一行打开环形缓冲区 ==========
    ShmRingBuf<JointCmd, 256> ring(SHM_NAME, Open);

    std::cout << "[CONSUMER] 已映射环形缓冲区: " << SHM_NAME << "\n"
              << "[CONSUMER] 等待关节指令... (Ctrl-C 退出)\n"
              << std::endl;

    JointCmd cmd;
    uint64_t processed = 0;

    while (running) {
        if (ring.read(&cmd) == 0) {
            processed++;

            // 计算延迟
            using namespace std::chrono;
            auto now = system_clock::now().time_since_epoch();
            uint64_t now_us = duration_cast<microseconds>(now).count();
            double latency_ms = (now_us - cmd.timestamp) / 1000.0;

            std::cout << std::fixed << std::setprecision(2)
                      << "[CONS] #" << std::setw(5) << cmd.sequence
                      << " | " << cmd.label
                      << " | q=[" << cmd.positions[0]
                      << " " << cmd.positions[1]
                      << " " << cmd.positions[2]
                      << " " << cmd.positions[3]
                      << " " << cmd.positions[4]
                      << " " << cmd.positions[5] << "]"
                      << " | dur=" << cmd.duration_ms << "ms"
                      << " | latency=" << latency_ms << "ms"
                      << " | buf=" << ring.count() << "/256"
                      << std::endl;

            // 模拟执行耗时 (比生产者慢一点, 看看缓冲区能不能兜住)
            std::this_thread::sleep_for(std::chrono::milliseconds(48));

        } else {
            // 缓冲区空 — 消费者比生产者快, 等待新数据
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }

    std::cout << "\n[CONSUMER] 停止, 共处理 " << processed << " 帧" << std::endl;
    return 0;
}

// ============================================================
// Main
// ============================================================
int main(int argc, char* argv[]) {
    if (argc < 2 || (strcmp(argv[1], "producer") != 0 && strcmp(argv[1], "consumer") != 0)) {
        std::cerr << "用法: " << argv[0] << " producer|consumer\n"
                  << "  终端1: " << argv[0] << " producer\n"
                  << "  终端2: " << argv[0] << " consumer\n";
        return 1;
    }

    try {
        if (strcmp(argv[1], "producer") == 0)
            return run_producer();
        else
            return run_consumer();
    } catch (const shm::Error& e) {
        std::cerr << "[FATAL] " << e.what() << std::endl;
        return 1;
    }
}
