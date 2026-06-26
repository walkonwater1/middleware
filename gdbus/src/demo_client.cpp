/**
 * demo_client.cpp — GDBus C++17 机器人监控客户端入口
 *
 * 模拟机器人远程监控终端：
 *   - 查询机器人状态 (GetRobotStatus)
 *   - 订阅状态变更/任务进度信号
 *   - 远程下发任务 (ExecuteTask / CancelTask)
 *   - 轮询属性 (State / BatteryLevel / Emotion / CpuTemp)
 *   - 紧急停止 (EmergencyStop)
 *
 * 所有 D-Bus 调用由 gdbus-codegen + gen_cpp_bindings.py 自动生成。
 * 修改 XML 后重新 cmake 即可获取最新接口。
 */

#include "gdbus_cxx.hpp"
#include "robot_bindings.hpp"   // ★ 自动生成

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

    g_print("╔══════════════════════════════════════════╗\n");
    g_print("║   机器人远程监控终端 (GDBus C++17)      ║\n");
    g_print("╚══════════════════════════════════════════╝\n\n");

    // 持久化上下文 — proxy 生命周期与 main() 一致
    struct AppCtx {
        gdbus_cxx::MainLoop* loop;
        std::unique_ptr<DemoRobotClient> proxy;
        int tick = 0;
    };

    struct ConnCtx {
        gdbus_cxx::MainLoop* loop;
        AppCtx* app;
    };

    AppCtx app{&loop, nullptr, 0};

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

            // 创建 Robot 代理
            ctx->app->proxy = DemoRobotClient::create_sync(conn,
                "com.example.RobotService", "/com/example/Robot");
            if (!ctx->app->proxy) {
                g_print("[CLIENT] 无法创建代理 (服务启动了?)\n");
                ctx->loop->quit();
                delete ctx;
                return;
            }

            auto* proxy = ctx->app->proxy.get();

            // ──────────────────────────────────────────────
            // 1. 订阅信号
            // ──────────────────────────────────────────────

            // StateChanged — 对应 RobotStateManager 状态转移
            proxy->on_state_changed([](const std::string& old_state,
                                        const std::string& new_state) {
                g_print("[CLIENT] <<< StateChanged: %s → %s\n",
                        old_state.c_str(), new_state.c_str());
            });

            // TaskProgress — 对应 TaskExecutor 任务进度回调
            proxy->on_task_progress([](const std::string& task_id,
                                        double progress,
                                        const std::string& status) {
                g_print("[CLIENT] <<< TaskProgress: %s %.0f%% (%s)\n",
                        task_id.c_str(), progress, status.c_str());
            });

            // BatteryLow — 低电量告警
            proxy->on_battery_low([](double level) {
                g_print("[CLIENT] <<< ⚠ BatteryLow: %.1f%% 剩余!\n", level);
            });

            // ──────────────────────────────────────────────
            // 2. 查询机器人状态 (同步方法)
            // ──────────────────────────────────────────────
            proxy->get_robot_status_async(
                [](const DemoRobotClient::GetRobotStatusResult& info) {
                    const auto& [robot_id, state, battery, cpu_temp] = info;
                    g_print("\n[CLIENT] === 机器人状态 ===\n");
                    g_print("  Robot ID:    %s\n", robot_id.c_str());
                    g_print("  State:       %s\n", state.c_str());
                    g_print("  Battery:     %.1f%%\n", battery);
                    g_print("  CPU Temp:    %.1f°C\n", cpu_temp);
                    g_print("========================\n\n");
                },
                [](const std::string& err) {
                    g_print("[CLIENT] GetRobotStatus 失败: %s\n", err.c_str());
                });

            // ──────────────────────────────────────────────
            // 3. 下发任务 (对应 TaskOrchestrator::StartTask)
            // ──────────────────────────────────────────────
            g_print("[CLIENT] 下发测试任务...\n");

            // Navigation 任务
            proxy->execute_task_async("Navigation", "前往充电桩",
                [](const DemoRobotClient::ExecuteTaskResult& r) {
                    if (r.success) {
                        g_print("[CLIENT] >>> ExecuteTask: Navigation → %s\n",
                                r.task_id.c_str());
                    }
                },
                [](const std::string& err) {
                    g_print("[CLIENT] ExecuteTask 失败: %s\n", err.c_str());
                });

            // Motion 任务
            proxy->execute_task_async("Motion", "挥手致意",
                [](const DemoRobotClient::ExecuteTaskResult& r) {
                    if (r.success) {
                        g_print("[CLIENT] >>> ExecuteTask: Motion → %s\n",
                                r.task_id.c_str());
                    }
                },
                [](const std::string& err) {
                    g_print("[CLIENT] ExecuteTask 失败: %s\n", err.c_str());
                });

            // VoiceInteraction 任务
            proxy->execute_task_async("VoiceInteraction", "问答对话",
                [](const DemoRobotClient::ExecuteTaskResult& r) {
                    if (r.success) {
                        g_print("[CLIENT] >>> ExecuteTask: VoiceInteraction → %s\n",
                                r.task_id.c_str());
                    }
                },
                [](const std::string& err) {
                    g_print("[CLIENT] ExecuteTask 失败: %s\n", err.c_str());
                });

            // ──────────────────────────────────────────────
            // 4. 定期轮询属性 (同步, 使用 GDBusProxy 缓存)
            // ──────────────────────────────────────────────
            // NOTE: 直接捕获 app/loop 的栈指针, 不依赖 conn_ctx
            // (conn_ctx 在下面被 delete, 不能从 timer 中引用)
            auto* timer_app = ctx->app;
            auto* timer_loop = ctx->loop;
            gdbus_cxx::timeout_add_seconds(3, [proxy, timer_app, timer_loop]() -> gboolean {
                timer_app->tick++;
                std::string state = proxy->get_state();
                double battery = proxy->get_battery_level();
                double cpu_temp = proxy->get_cpu_temp();
                std::string emotion = proxy->get_emotion();

                g_print("[CLIENT] [轮询#%d] 状态=%s 电量=%.1f%% CPU=%.1f°C 表情=%s\n",
                        timer_app->tick, state.c_str(), battery, cpu_temp,
                        emotion.c_str());

                // 运行 ~24 秒后退出
                if (timer_app->tick >= 8) {
                    g_print("\n[CLIENT] 演示完成, 退出.\n");
                    timer_loop->quit();
                    return G_SOURCE_REMOVE;
                }

                return G_SOURCE_CONTINUE;
            });

            g_print("[CLIENT] 已就绪, 按 Ctrl+C 退出\n");
            // conn_ctx 使命完成 — timer 只依赖 &app 和 &loop (均在 main() 栈上)
            delete ctx;
        }, conn_ctx);

    g_print("正在连接 D-Bus session bus...\n");
    loop.run();
    g_print("客户端已停止.\n");
    return 0;
}
