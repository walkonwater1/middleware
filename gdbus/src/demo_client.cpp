/**
 * demo_client.cpp — GDBus C++17 车辆信息客户端入口
 *
 * 所有 D-Bus 调用由 gdbus-codegen + gen_cpp_bindings.py 自动生成。
 * 修改 XML 后重新 cmake 即可获取最新接口。
 */

#include "gdbus_cxx.hpp"
#include "vehicle_bindings.hpp"   // ★ 自动生成

#include <clocale>
#include <csignal>
#include <cstdio>

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

    g_print("=== GDBus C++17 车辆信息客户端 ===\n\n");

    // 持久化上下文 — proxy 生命周期与 main() 一致
    struct AppCtx {
        gdbus_cxx::MainLoop* loop;
        std::unique_ptr<DemoVehicleClient> proxy;
    };

    struct ConnCtx {
        gdbus_cxx::MainLoop* loop;
        AppCtx* app;
    };

    AppCtx app{&loop, nullptr};
    g_loop = &loop;

    auto* conn_ctx = new ConnCtx{&loop, &app};
    g_dbus_connection_new_for_address(
        g_getenv("DBUS_SESSION_BUS_ADDRESS"),
        static_cast<GDBusConnectionFlags>(
            G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT |
            G_DBUS_CONNECTION_FLAGS_MESSAGE_BUS_CONNECTION),
        nullptr, nullptr,
        [](GObject*, GAsyncResult* res, gpointer data) {
            auto* ctx = static_cast<ConnCtx*>(data);
            GError* err = nullptr;
            GDBusConnection* conn = g_dbus_connection_new_for_address_finish(res, &err);
            if (err) {
                g_print("[CLIENT] 连接失败: %s\n", err->message);
                g_error_free(err);
                ctx->loop->quit();
                delete ctx;
                return;
            }

            g_print("[CLIENT] D-Bus 连接成功\n");

            // ★ 自动生成的代理 — 所有权转移给 app，生命周期与 main() 一致
            ctx->app->proxy = DemoVehicleClient::create_sync(conn);
            if (!ctx->app->proxy) {
                g_print("[CLIENT] 无法创建代理 (服务启动了?)\n");
                ctx->loop->quit();
                delete ctx;
                return;
            }

            auto* proxy = ctx->app->proxy.get();

            // 1. 订阅 SpeedChanged 信号
            proxy->on_speed_changed([](double s) {
                g_print("[CLIENT] <<< SpeedChanged: %.1f km/h\n", s);
            });

            // 2. 调用 GetVehicleInfo 方法
            proxy->get_vehicle_info_async(
                [](const DemoVehicleClient::GetVehicleInfoResult& info) {
                    const auto& [vehicle_id, speed, odo] = info;  // C++17 结构化绑定
                    g_print("[CLIENT] GetVehicleInfo:\n");
                    g_print("  Vehicle ID: %s\n", vehicle_id.c_str());
                    g_print("  Speed:      %.1f km/h\n", speed);
                    g_print("  Odometer:   %.1f km\n", odo);
                },
                [](const std::string& err) {
                    g_print("[CLIENT] 调用失败: %s\n", err.c_str());
                });

            // 3. 每5秒读取 Speed 属性 (缓存值, 同步快速)
            gdbus_cxx::timeout_add_seconds(5, [proxy]() -> gboolean {
                double s = proxy->get_speed();
                g_print("[CLIENT] Speed 属性 = %.1f km/h\n", s);
                return G_SOURCE_CONTINUE;
            });

            g_print("[CLIENT] 已就绪, 按 Ctrl+C 退出\n\n");
            delete ctx;
        }, conn_ctx);

    g_print("正在连接 D-Bus session bus...\n");
    loop.run();
    g_print("客户端已停止.\n");
    return 0;
}
