/**
 * demo_service.cpp — GDBus C++17 车辆信息服务端入口
 *
 * 工作流 (修改接口只需第1步):
 *   1. 编辑 interfaces/com.example.Vehicle.xml
 *   2. cmake .. && make  (自动生成 C + C++ 代码)
 *   3. 本站只写业务逻辑
 */

#include "gdbus_cxx.hpp"
#include "vehicle_bindings.hpp"   // ★ 自动生成, 来自 gen_cpp_bindings.py

#include <atomic>
#include <clocale>
#include <csignal>
#include <cstdio>
#include <random>

namespace {

gdbus_cxx::MainLoop* g_loop = nullptr;

void on_signal(int sig) {
    g_print("\n收到信号 %d, 退出...\n", sig);
    if (g_loop) g_loop->quit();
}

} // namespace

int main() {
    setlocale(LC_ALL, "");
    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);

    gdbus_cxx::MainLoop loop;
    g_loop = &loop;

    // 模拟车辆状态
    std::atomic<double> speed{0.0};
    std::atomic<double> odometer{12345.6};
    const std::string vehicle_id = "VIN-ABC-123456789";

    // ★ 自动生成的骨架类 — 方法/信号/属性由 XML 定义
    DemoVehicleServer skel;

    // 只需设置业务回调 — 方法签名由生成代码保证类型安全
    skel.set_get_vehicle_info_handler([&]() -> DemoVehicleServer::GetVehicleInfoResult {
        return {vehicle_id, speed.load(), odometer.load()};
    });

    // 获取总线名
    gdbus_cxx::BusName bus;
    bus.own("com.example.VehicleService",
        [&](GDBusConnection* conn) {
            g_print("=== GDBus C++17 车辆信息服务 ===\n");
            g_print("接口定义: interfaces/com.example.Vehicle.xml\n");
            g_print("代码生成: gdbus-codegen → C skeleton\n");
            g_print("          gen_cpp_bindings.py → C++ wrapper\n\n");

            skel.export_on_bus(conn, "/com/example/Vehicle");

            // 速度模拟定时器
            gdbus_cxx::timeout_add_seconds(2, [&]() -> gboolean {
                thread_local std::mt19937 gen(std::random_device{}());
                thread_local std::uniform_real_distribution<double> dist(-5.0, 5.0);

                double s = speed.load() + dist(gen);
                if (s < 0.0) s = 0.0;
                if (s > 120.0) s = 120.0;

                speed.store(s);
                odometer.store(odometer.load() + s / 3600.0 * 2.0);

                g_print("[SERVICE] 速度: %.1f km/h\n", s);
                skel.set_speed(s);                  // ★ 自动生成
                skel.emit_speed_changed(s);         // ★ 自动生成

                return G_SOURCE_CONTINUE;
            });

            g_print("[SERVICE] 已启动 (2s 间隔), 按 Ctrl+C 退出\n\n");
        },
        [&]() {
            g_print("[SERVICE] 总线名丢失!\n");
            loop.quit();
        });

    loop.run();
    g_print("服务已停止.\n");
    return 0;
}
