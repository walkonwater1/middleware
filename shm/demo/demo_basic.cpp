/**
 * demo_basic.cpp — 使用 shm_cxx.hpp 实现车辆传感器数据共享
 *
 * 对比原 shm_writer.c / shm_reader.c:
 *   - 零手动 shm_open/mmap/ftruncate/munmap/close/unlink
 *   - RAII 自动清理, 哪怕中途抛异常也不会泄漏
 *   - 类型安全: shm->speed = 120 而不是 memcpy 裸指针
 *
 * 用法:
 *   ./demo_basic writer    # 终端 1 — 写入端
 *   ./demo_basic reader    # 终端 2 — 读取端
 */

#include <iostream>
#include <iomanip>
#include <cstring>
#include <csignal>
#include <chrono>
#include <thread>
#include <atomic>

#include "shm_cxx.hpp"

using namespace shm;

// ============================================================
// 共享内存数据结构 — 定义一次, 两端共享
// ============================================================
struct alignas(64) VehicleSensor {   // cacheline 对齐, 避免 false sharing
    uint64_t sequence      = 0;
    uint64_t timestamp_us  = 0;
    double   speed         = 0.0;     // km/h
    double   temperature   = 0.0;     // °C
    double   latitude      = 0.0;
    double   longitude     = 0.0;
    double   battery_soc   = 0.0;     // %
    int32_t  status        = 0;       // 0=STOP 1=RUN 2=CHARGE 3=FAULT
    char     vehicle_id[32] = {};
    char     padding[28]    = {};
};
static_assert(sizeof(VehicleSensor) == 128, "padding check");

constexpr const char* SHM_NAME = "/vehicle_sensor_cxx";

// ============================================================
// 模拟传感器数据生成
// ============================================================
static void generate(VehicleSensor& s, uint64_t seq) {
    using namespace std::chrono;
    auto now = system_clock::now().time_since_epoch();
    s.sequence     = seq;
    s.timestamp_us = duration_cast<microseconds>(now).count();
    s.speed        = (rand() % 12000) / 100.0;
    s.temperature  = 70.0 + (rand() % 4000) / 100.0;
    s.latitude     = 22.53 + (rand() % 400 - 200) / 10000.0;
    s.longitude    = 113.93 + (rand() % 400 - 200) / 10000.0;
    s.battery_soc  = 85.0 - seq * 0.05;
    s.status       = (seq > 20 && seq <= 25) ? 3 : 1;
    strncpy(s.vehicle_id, "VIN-CXX-001", sizeof(s.vehicle_id) - 1);
}

static const char* status_name(int s) {
    static const char* names[] = {"STOP", "RUN", "CHARGE", "FAULT"};
    return (s >= 0 && s <= 3) ? names[s] : "?";
}

// ============================================================
// Writer
// ============================================================
static std::atomic<bool> running{true};

void on_signal(int) { running = false; }

static int run_writer() {
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    // ========== 核心: 3 行代码完成共享内存创建 ==========
    Shm<VehicleSensor> shm(SHM_NAME, Create);
    // shm 析构时自动: munmap → close → shm_unlink

    std::cout << "[WRITER] 共享内存已创建: " << SHM_NAME << "\n"
              << "[WRITER] 大小: " << sizeof(VehicleSensor) << " bytes\n"
              << "[WRITER] 开始写入 (1Hz, Ctrl-C 退出)...\n"
              << std::endl;

    uint64_t seq = 0;
    while (running) {
        seq++;
        generate(*shm, seq);   // 直接写入共享内存, 零拷贝!

        std::cout << std::fixed << std::setprecision(1)
                  << "[WRITE] #" << std::setw(3) << seq
                  << " | speed=" << shm->speed << " km/h"
                  << " | temp=" << shm->temperature << "°C"
                  << " | battery=" << shm->battery_soc << "%"
                  << " | status=" << status_name(shm->status)
                  << std::endl;

        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    std::cout << "\n[WRITER] 停止, 共享内存自动清理" << std::endl;
    return 0;
}

// ============================================================
// Reader
// ============================================================
static int run_reader() {
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    // ========== 核心: 2 行代码完成共享内存打开 ==========
    Shm<VehicleSensor> shm(SHM_NAME, Open);
    // shm 析构时自动: munmap → close (不 unlink, 因为不是 owner)

    std::cout << "[READER] 已映射共享内存: " << SHM_NAME << "\n"
              << "[READER] 等待数据... (Ctrl-C 退出)\n"
              << std::endl;

    uint64_t last_seq = 0;
    while (running) {
        if (shm->sequence != last_seq) {
            last_seq = shm->sequence;

            // 计算传输延迟
            using namespace std::chrono;
            auto now = system_clock::now().time_since_epoch();
            uint64_t now_us = duration_cast<microseconds>(now).count();
            double latency_us = static_cast<double>(now_us - shm->timestamp_us);

            std::cout << std::fixed << std::setprecision(1)
                      << "[READ]  #" << std::setw(3) << shm->sequence
                      << " | id=" << shm->vehicle_id
                      << " | speed=" << shm->speed
                      << " | temp=" << shm->temperature
                      << " | pos=(" << shm->latitude << "," << shm->longitude << ")"
                      << " | battery=" << shm->battery_soc << "%"
                      << " | latency=" << latency_us << "us" << std::flush;

            if (shm->status == 3) {
                std::cout << " ⚠ FAULT!";
            }
            std::cout << std::endl;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    std::cout << "\n[READER] 停止" << std::endl;
    return 0;
}

// ============================================================
// Main
// ============================================================
int main(int argc, char* argv[]) {
    if (argc < 2 || (strcmp(argv[1], "writer") != 0 && strcmp(argv[1], "reader") != 0)) {
        std::cerr << "用法: " << argv[0] << " writer|reader\n"
                  << "  终端1: " << argv[0] << " writer\n"
                  << "  终端2: " << argv[0] << " reader\n";
        return 1;
    }

    try {
        if (strcmp(argv[1], "writer") == 0)
            return run_writer();
        else
            return run_reader();
    } catch (const shm::Error& e) {
        std::cerr << "[FATAL] " << e.what() << std::endl;
        return 1;
    }
}
