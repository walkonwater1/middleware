/**
 * demo_service.cpp — GDBus C++17 Vehicle Service Demo Entry Point
 *
 * Starts the vehicle info service and runs the GLib main loop.
 * Press Ctrl+C to gracefully shut down.
 *
 * Demonstrates C++17 features:
 *   - RAII resource management (BusName, NodeInfo, MainLoop)
 *   - std::atomic for lock-free thread-safe state
 *   - Lambda captures for callback-based APIs
 *   - auto type deduction
 */

#include "vehicle_service.hpp"
#include "gdbus_cxx.hpp"

#include <clocale>
#include <csignal>
#include <cstdio>
#include <cstdlib>

namespace {

// Signal handler to gracefully quit the main loop
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

    // Install signal handlers for graceful shutdown
    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);

    // Create main loop (RAII)
    gdbus_cxx::MainLoop loop;
    g_loop = &loop;

    // Create and start the vehicle service (RAII — BusName, timers, etc.)
    VehicleService svc;
    svc.start();

    // Run the event loop (blocks until quit() is called)
    loop.run();

    g_print("服务已停止.\n");
    return 0;
}
