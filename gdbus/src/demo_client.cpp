/**
 * demo_client.cpp — GDBus C++17 Vehicle Client Demo Entry Point
 *
 * Connects to the VehicleService, calls methods, subscribes to signals,
 * and polls properties — all using C++17 async patterns.
 *
 * Demonstrates C++17 features:
 *   - std::function for type-erased callbacks
 *   - Structured bindings (VehicleInfo struct)
 *   - std::chrono for time intervals
 *   - Lambda expressions for inline callback definitions
 *   - RAII SignalSubscription
 */

#include "vehicle_client.hpp"
#include "gdbus_cxx.hpp"

#include <clocale>
#include <csignal>
#include <cstdio>
#include <cstdlib>

namespace {

gdbus_cxx::MainLoop* g_loop = nullptr;

void on_signal(int sig) {
    g_print("\n收到信号 %d, 正在退出...\n", sig);
    if (g_loop) {
        g_loop->quit();
    }
}

} // anonymous namespace

int main() {
    // Initialize locale for proper UTF-8 output (fixes garbled Chinese text)
    setlocale(LC_ALL, "");

    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);

    gdbus_cxx::MainLoop loop;
    g_loop = &loop;

    // Create client and wire up callbacks
    VehicleClient client;

    // Callback: received SpeedChanged signal
    client.on_speed_changed = [](double new_speed) {
        // Could update a UI, log to file, etc.
        g_print("[APP] 速度更新: %.1f km/h\n", new_speed);
    };

    // Callback: received GetVehicleInfo result
    client.on_info_received = [](const VehicleInfo& info) {
        // C++17 structured bindings — destructure the result
        const auto& [id, speed, odo] = info;
        g_print("[APP] 车辆信息更新: id=%s, speed=%.1f, odo=%.1f\n",
                id.c_str(), speed, odo);
    };

    // Callback: error handling
    client.on_error = [](const std::string& error) {
        g_print("[APP] 错误: %s\n", error.c_str());
    };

    // Connect asynchronously — callbacks fire when ready
    client.connect();

    g_print("按 Ctrl+C 退出\n\n");

    // Run event loop
    loop.run();

    g_print("客户端已停止.\n");
    return 0;
}
